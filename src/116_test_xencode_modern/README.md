# 116_test_xencode_modern —— 现代 C++ 风格的 FFmpeg 编码器封装

这是 `../116_test_xencode/` 的重写版。功能完全一样（合成 1920×1080@25 的 YUV，
用 libx264/libx265 编成裸流写文件），但接口形式和内部实现全部重做：

| | 旧版 `XEncode` | 本版 `xencode` |
| --- | --- | --- |
| 头文件里的 FFmpeg 类型 | `AVCodecContext*`、前向声明、`int codec_id` | **零**。连 `AVPacket` 这个名字都不出现 |
| 调用方需要 `#include <libavcodec/...>` | 是 | **否** |
| 资源释放 | 手工 `av_packet_free` / `av_frame_free` | RAII，调用方看不到 free |
| 失败表示 | `nullptr` / 空 `vector` / 裸 `int` | `std::expected<T, Error>` |
| "没有数据" vs "出错" | 都是空 `vector`，分不清 | 类型上分开 |
| 参数只能 set 在 open 之前 | 靠注释 + 隐式契约 | **编译期**由类型分离保证 |
| 平面尺寸 | 调用方自己按 `linesize` 算，算错就 SIGSEGV | `PlaneView` 自带正确宽高，`row()` 带边界检查 |
| `send_frame` 返回 `EAGAIN` | 静默丢帧 | 内部重试，绝不丢帧 |
| 线程安全 | 一把大锁包住所有 FFmpeg 调用，声称"线程安全" | 明确标注非线程安全，不用假锁 |
| 平面越界 bug | 写得出（就是上次崩的那个） | **写不出来** |

需要 **C++23**（`std::expected`、`std::span`）。

---

## 1. 目录结构

```
116_test_xencode_modern/
├── include/xencode/xencode.h      ← 唯一的公开头文件，编译它不需要 FFmpeg
├── src/
│   ├── detail/ffi.h               ← 内部头文件：唯一 include libav* 的地方
│   ├── errors.cpp                 ← 错误码文本 / 配置校验
│   ├── frame.cpp                  ← Frame + PlaneView
│   ├── packet.cpp                 ← Packet
│   ├── encoder.cpp                ← EncoderBuilder / Encoder
│   └── bitstream_writer.cpp       ← 裸流写文件
├── demo/main.cpp                  ← 调用示例（同样不 include 任何 libav*）
└── CMakeLists.txt
```

**`src/` 目录对外不可见**：`xencode` 这个 CMake target 只把 `include/` 声明为
`PUBLIC` 头文件路径，`src/` 是 `PRIVATE`；`demo` 只链接 `xencode`，不链接任何
`av*` 库。也就是说调用方**物理上**拿不到 `AVPacket*`。

---

## 2. 核心设计

### 2.1 pImpl：把 FFmpeg 彻底挡在编译边界之外

```cpp
// include/xencode/xencode.h —— 这是调用方能看到的全部
namespace xencode {
namespace detail { struct FrameImpl; struct PacketImpl; struct EncoderImpl; struct FfiAccess; }

class Frame {
public:
  struct PlaneView { std::span<std::uint8_t> pixels; int stride, width, height;
                     std::span<std::uint8_t> row(int y) noexcept; };
  Result<PlaneView> plane(Plane p) noexcept;
  Result<void> make_writable();
  void set_pts(std::int64_t) noexcept;
  // ...
private:
  friend struct detail::FfiAccess;
  std::unique_ptr<detail::FrameImpl> impl_;   // ← 不完整类型，内容在 .cpp 里
};
}
```

验证过：单独编译这个头文件，**完全不需要任何 FFmpeg 头文件**。

```bash
g++ -std=c++23 -Iinclude -fsyntax-only -xc++ - <<< '#include <xencode/xencode.h>'
```

好处不只是"头文件干净"：

- 改 FFmpeg 版本、换实现（比如换成硬编 NVDEC/NVENC）不需要重编所有调用方；
- 不会因为某个宏（`__STDC_CONSTANT_MACROS`、`av_...` 宏污染）跟调用方冲突；
- 单元测试可以给 `xencode` 换个假实现而不带 FFmpeg 依赖。

### 2.2 裸指针只在一个特权结构里出现

`src/detail/ffi.h` 定义了 `detail::FfiAccess`，它是 `Frame` / `Packet` / `Encoder`
在公开头文件里声明过的 `friend`：

```cpp
struct FfiAccess {
  static AVFrame        *raw(Frame &) noexcept;
  static AVPacket       *raw(Packet &) noexcept;
  static AVCodecContext *raw(Encoder &) noexcept;
  static Frame   adopt(AVFrame *frame) noexcept;    // 接管所有权
  static Packet  adopt(AVPacket *packet) noexcept;
};
```

FFmpeg 的裸指针只在这一个结构里进出，底层对象本身由
`std::unique_ptr<T, Deleter>` 持有：

```cpp
struct FrameDeleter  { void operator()(AVFrame *f)  const noexcept { av_frame_free(&f); } };
struct PacketDeleter { void operator()(AVPacket *p) const noexcept { av_packet_free(&p); } };
struct CtxDeleter    { void operator()(AVCodecContext *c) const noexcept { avcodec_free_context(&c); } };
```

于是 `Frame` / `Packet` / `Encoder` 全是 **move-only + Rule of Zero**：
copy 自动禁用，move 自动正确，不可能 double free，也不需要写析构函数。

### 2.3 阶段化类型：把"调用顺序"变成编译期约束

旧版的核心问题之一是隐式契约：`SetOpt` 必须在 `Open` 之前、`width/height` 必须在
`CreateFrame` 之前。它靠注释维持，加 mutex 兜底，但违反时依然静默出错。

新版把生命周期切成两个**不同的类型**：

```cpp
class EncoderBuilder {                     // 只能配置，不能编码
public:
  EncoderBuilder &set_option(std::string_view key, std::string_view value);
  Result<Encoder> open();                  // 唯一的出口
};

class Encoder {                            // 只能编码，没有 set_option
public:
  Result<Frame> create_frame() const;
  Result<std::vector<Packet>> encode(const Frame &);
  Result<std::vector<Packet>> flush();
};
```

`open()` 之后拿到的 `Encoder` 上**没有** `set_option()`，所以
"`av_opt_set` 必须在 `avcodec_open2` 之前"这条规则写错就编译不过：

```cpp
auto enc = xencode::make_encoder(cfg);
enc->set_option("preset", "fast");   // ← 编译错误，不是运行期告警
```

`flush()` 之后继续 `encode()` 也不再是 UB，而是返回 `ErrorCode::already_flushed`
（用显式 `bool flushed` 状态机，替代旧版"靠 `AVERROR_EOF` 静默兜底"）。

### 2.4 平面视图：让上次那个越界 bug 写不出来

```cpp
struct PlaneView {
  std::span<std::uint8_t> pixels;   // 整块平面（含 padding）
  int stride;                       // 行字节数
  int width;                        // ← U/V 这里的值就是 width/2
  int height;                       // ← U/V 这里的值就是 height/2
  std::span<std::uint8_t> row(int y) noexcept;   // 越界返回空 span
};
```

`width` / `height` 由 `av_pix_fmt_desc_get()` 的 `log2_chroma_w/h` 折算出来，
不再由调用方猜：

```cpp
auto luma   = frame.plane(Plane::y);   // width=1920, height=1080
auto chroma = frame.plane(Plane::u);   // width= 960, height= 540   ← 自动折半
```

于是正常写法：

```cpp
for (int y = 0; y < view.height; ++y) {
  auto line = view.row(y);          // 长度恰好是 view.width，且带边界检查
  for (int x = 0; x < view.width; ++x)
    line[x] = pattern(x, y);
}
```

**这个循环不可能越界**——因为色度平面的 `view.height` 就是 540，不是 1080。
旧版那个 `c->height / c->width` 的 bug 在新接口下没有表达方式。

### 2.5 错误处理：`std::expected`

```cpp
Result<std::vector<Packet>> Encoder::encode(const Frame &frame);
```

- 成功且暂时没有输出 → `std::vector<Packet>{}`（**正常情况**，B 帧/前瞻缓冲）
- 失败 → `Error{ code, ffmpeg_error, detail }`

旧版两者都是"空 vector"，调用方无从区分。

`Error::ffmpeg_error` 保留 `AVERROR()` 原始值，`describe()` 会调 `av_strerror`
翻译成可读文本——但这层翻译发生在 `.cpp` 里，调用方只见到 `std::string`。

### 2.6 不丢帧

旧版 `Encode()`：

```cpp
auto re = avcodec_send_frame(c_, frame);
if (re < 0 && re != AVERROR(EAGAIN)) { ... return {}; }   // EAGAIN 直接放过 → 丢帧
```

`EAGAIN` 的语义是"**输入队列满，本帧没有被接收**"。新版：

```cpp
int ret = avcodec_send_frame(ctx, raw_frame);
while (ret == AVERROR(EAGAIN)) {
  auto drained = drain_packets(ctx);      // 先把输出抽干腾出空间
  if (!drained) return std::unexpected(drained.error());
  append_packets(output, std::move(*drained));
  ret = avcodec_send_frame(ctx, raw_frame);   // 再重试同一帧
}
```

### 2.7 线程模型说实话

`AVCodecContext` 的 `send_frame` / `receive_packet` 对同一实例并发调用是 UB。
旧版加了一把 `std::mutex` 把整个 FFmpeg 调用串起来，对外宣称"线程安全"——
这只是把并发变串行，并没有让契约成立，而且析构函数还漏了加锁。

新版的做法：**明确声明非线程安全**，需要并发就每个线程一个 `Encoder`。
`Encoder` 也确实是 move-only 值类型，天然适合放进 `thread_local` 或每线程上下文。

---

## 3. 新旧 API 对照

```cpp
// ---------- 旧版 ----------
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

XEncode en;
auto c = XEncode::Create(AV_CODEC_ID_H264);
en.set_c(c);
c->width = 1920;  c->height = 1080;
en.SetOpt("crf", 18);
c->rc_max_rate = 800000;  c->rc_buffer_size = 1600000;
en.Open();
AVFrame *frame = en.CreateFrame();
frame->pts = i;
av_frame_make_writable(frame);
frame->data[1][y * frame->linesize[1] + x] = ...;   // ← 越界就在这里
std::vector<AVPacket *> pkts = en.Encode(frame);
for (auto *pkt : pkts) { ofs.write((char *)pkt->data, pkt->size); av_packet_free(&pkt); }
```

```cpp
// ---------- 新版 ----------
#include <xencode/xencode.h>

xencode::EncoderConfig cfg;
cfg.width = 1920;  cfg.height = 1080;  cfg.fps = 25;
cfg.rate_control = xencode::RateControl::constant_quality_capped;
cfg.constant_quality = 18;
cfg.bitrate_kbps = 800;  cfg.buffer_kbps = 1600;

auto enc = xencode::make_encoder(cfg);                       // Result<Encoder>
if (!enc) { std::cerr << xencode::describe(enc.error()); return 1; }

auto frame = enc->create_frame();                            // Result<Frame>
for (int i = 0; i < 1000; ++i) {
  if (auto ok = frame->make_writable(); !ok) { /* ... */ }
  if (auto chroma = frame->plane(xencode::Plane::u); chroma) {
    for (int y = 0; y < chroma->height; ++y) {               // ← 540，自动
      auto line = chroma->row(y);
      for (int x = 0; x < chroma->width; ++x) line[x] = ...; // ← 960，自动
    }
  }
  frame->set_pts(i);
  auto packets = enc->encode(*frame);                        // Result<vector<Packet>>
  writer->write_all(*packets);
}
auto tail = enc->flush();
```

---

## 4. 验证

### 4.1 功能等价（逐字节）

和 `../116_test_xencode_fixed/` 用相同的合成图像、相同参数编码，产物**逐字节一致**：

```bash
# 修复版：把 xencode.cpp 里 thread_count 改成 1 后编码
# 现代版：
cd 116_test_xencode_modern/build
./xencode_demo h264 1
cmp <修复版产物> 1920_1080_25_h264_constant_quality_capped.264   # → 无输出，完全相同
```

### 4.2 一个值得注意的坑：`thread_count > 1` 时输出不可复现

排查上面这条比对时发现的：**旧版代码，同一份二进制连续跑三次，md5 全都不一样**。

```
r1.264  4023821  md5 7de502ac...
r2.264  4025602  md5 46f02428...
r3.264  4028639  md5 82b94e6c...
```

把 `c->thread_count` 从 16 改成 1 之后，两次运行 md5 完全相同。
所以这是 x264 多线程（frame threads + lookahead threads 的速率控制决策受调度影响）
导致的，不是封装层的问题。**做产物比对时务必单线程**。

### 4.3 其余检查

```bash
./xencode_demo                 # H.264  1000 帧
./xencode_demo h265 4          # HEVC   1000 帧
./xencode_demo bogus           # 未知编码器 → 回退 h264 并提示
```

```
codec_name=h264  width=1920  height=1080  nb_read_frames=1000  avg_frame_rate=25/1
codec_name=hevc  width=1920  height=1080  nb_read_frames=1000
```

两个版本的产物 PSNR（互比，同源但线程导致的决策差异）：`psnr_avg ≈ 40 dB`。

---

## 5. 构建与运行

```bash
cd 116_test_xencode_modern
cmake -S . -B build          # C++23，默认 Debug
cmake --build build -j
cd build
./xencode_demo                # -> 1920_1080_25_h264_constant_quality_capped.264
./xencode_demo h265 4         # -> 1920_1080_25_hevc_constant_quality_capped.265
```

编译选项：`-Wall -Wextra -Wpedantic -Wshadow`，当前 **零警告**。

关于 RPATH：必须用 `-Wl,--disable-new-dtags` 生成 `DT_RPATH` 而不是默认的
`DT_RUNPATH`。因为 `DT_RUNPATH` 只对**本对象的直接依赖**生效，而
`libavcodec.so.58` 自己要找 `libswresample.so.3`，用 `DT_RUNPATH` 时会去系统路径找，
导致 `error while loading shared libraries: libswresample.so.3`。

---

## 6. 还可以继续往下做

按这四条路继续演进，都很自然：

1. **静态多态代替虚函数**
   如果以后要接硬件编码器（NVENC / VAAPI / VideoToolbox），别急着上 `virtual`：
   C++20 concepts + 模板可以在编译期分派，零运行时开销：
   ```cpp
   template <class Backend>
   concept EncodeBackend = requires(Backend b, const Frame& f) {
     { b.encode(f) } -> std::same_as<Result<std::vector<Packet>>>;
     { b.flush() }   -> std::same_as<Result<std::vector<Packet>>>;
   };
   ```
   需要运行期插件式热插拔时再用 `virtual` + 工厂——那才是该付虚表代价的地方。

2. **位流格式显式化**
   现在依赖 libx264/libx265 恰好输出 Annex-B。正确做法是把 `Packet` 分成
   "裸 NAL" 和 "Annex-B / AVCC" 两种类型，或者内部挂一个 `bsf`
   （`h264_mp4toannexb` / `hevc_annexb`），让输出格式成为类型的一部分而不是隐含约定。

3. **`extradata` / 参数集**
   目前 SPS/PPS 跟着 IDR 一起吐出来（`AV_PKT_FLAG_KEY`），所以裸流能播。
   若要走 `AV_CODEC_FLAG_GLOBAL_HEADER` 路线，就需要把 `extradata` 也包成一个
   显式对象 `Parameters` 暴露出去，而不是让调用方去摸 `ctx->extradata`。

4. **合成器 / 消费者管线**
   `Frame` 现在是"可写的哑缓冲区"。真实项目里会希望 `FrameSource`（生成/解码）
   和 `FrameSink`（编码/渲染）之间用 `std::function` / `concept` 连接，
   编码器只负责 `encode(Frame) -> vector<Packet>`，不再关心数据从哪来。
