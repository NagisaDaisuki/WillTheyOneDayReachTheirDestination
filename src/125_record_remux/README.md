---
title: 125_record_remux：解封装→解码→缩放→编码→重封装全链路（含 8 个时间戳/流参数坑）
publishedAt: 2026-11-27
description: 用 FFmpeg C API 把任意视频转成指定分辨率的 H.265 MP4，按秒截断；从封装/解封装的底层概念讲起，逐层拆解 7 个类与 main 的协作，并复盘「音频正常、视频花屏」背后的 8 个真实 bug。
tags: [AI, FFmpeg, C++, 音视频, 解封装, 封装]
category: 音视频开发
draft: false
---

# 125_record_remux

一个**完整转码链路**的小工具：解封装 → 解码 → `sws_scale` 缩放 → 重新编码（统一 H.265）→ 重封装，支持按秒截断。

```bash
./125_record_remux input.mp4 output.mp4 10 30 1280 720
#                                 ↑   ↑   ↑     ↑
#                              起始秒 结束秒  宽    高
```

**输出**：`hevc / mp4 (hev1)`，时间轴从 0 开始，分辨率等于命令行给定值。

---

## 第一部分：封装与解封装基础

### 一、三层模型：容器 / 编码 / 像素

音视频文件是一个俄罗斯套娃，从上到下三层：

```text
┌───────────────────────────────────────────────────────────┐
│ 容器层（Container / Format，如 mp4 / mkv / flv / ts）      │
│  负责：把多条流交织存放、记录时间轴、存索引、存元数据      │
│  FFmpeg：AVFormatContext + AVStream + AVPacket(裸包)      │
├───────────────────────────────────────────────────────────┤
│ 编码层（Codec，如 h264 / hevc / aac / opus）              │
│  负责：压缩/解压。P/B 帧依赖、GOP、量化、码率控制          │
│  FFmpeg：AVCodecContext + AVPacket(压缩数据)              │
├───────────────────────────────────────────────────────────┤
│ 像素/采样层（Raw，如 yuv420p / fltp）                     │
│  负责：真正的画面和声音                                    │
│  FFmpeg：AVFrame                                          │
└───────────────────────────────────────────────────────────┘
```

由此得到四组动作：

| 动作 | 方向 | 输入 → 输出 | 是否解码 |
| --- | --- | --- | --- |
| **解封装 demux** | 下 | 容器 → 压缩包 `AVPacket` | 否 |
| **解码 decode** | 下 | `AVPacket` → 像素帧 `AVFrame` | — |
| **缩放 filter** | 平 | `AVFrame` → 新尺寸 `AVFrame` | — |
| **编码 encode** | 上 | `AVFrame` → 压缩包 `AVPacket` | — |
| **封装 mux** | 上 | `AVPacket` → 新容器 | 否 |

两个容易混的概念：

- **转封装 remux** = demux + mux，**不解码不编码**，只是换个盒子。快、无损、画质不变，但改不了编码格式和分辨率。
- **转码 transcode** = demux + decode + (filter) + encode + mux。慢、有损，但能改编码、改分辨率、改码率。

本项目做的是**后者**，因为要改分辨率、要把编码统一成 H.265。

### 二、对应的核心结构体

| 结构体 | 归属 | 一句话职责 |
| --- | --- | --- |
| `AVFormatContext` | 封装 | 一次容器会话的总句柄，持有 `AVStream[]` 和 IO |
| `AVStream` | 封装 | 容器里的一条流：索引、`time_base`、`AVCodecParameters`、帧率 |
| `AVCodecParameters` | 编码 | 流的编码参数（codec_id、宽高、extradata、采样率…）**不含运行状态** |
| `AVCodecContext` | 编码 | 编解码器实例：参数 + 内部状态 + 线程 |
| `AVPacket` | 传输 | 一段压缩数据 + `pts/dts/duration/stream_index/flags` |
| `AVFrame` | 传输 | 一帧原始数据 + `data/linesize/width/height/format/pts` |
| `SwsContext` | 图像 | 缩放/色彩空间转换实例 |

关键区别：`AVCodecParameters`（描述）vs `AVCodecContext`（干活）。容器里存的是前者，编解码器要的是后者，两者之间用 `avcodec_parameters_to_context` / `from_context` 搬运。

### 三、时间基 time_base：本项目的万恶之源

#### 3.1 定义

`time_base = num/den` 表示「一个时间戳单位 = num/den 秒」。

```text
秒数 = pts × av_q2d(time_base)
```

同一个 1 秒的画面，在不同时间基下 pts 完全不同：

| time_base | 1 秒对应的 pts |
| --- | --- |
| 1/24 | 24 |
| 1/25 | 25 |
| 1/90000 | 90000 |
| 1/12288 | 12288 |

**每条流、每个编解码器、每个封装上下文都有自己独立的时间基。** 跨对象传递 pts 时必须换算，否则就是本项目最初遇到的灾难。

#### 3.2 换算 API

| 函数 | 用途 |
| --- | --- |
| `av_rescale_q(a, from_tb, to_tb)` | `a` 从 `from_tb` 换到 `to_tb`（四舍五入） |
| `av_rescale_q_rnd(a, from, to, rnd)` | 同上，可指定取整方式（`AV_ROUND_NEAR_INF \| AV_ROUND_PASS_MINMAX` 最常用） |
| `av_packet_rescale_ts(pkt, from_tb, to_tb)` | 一次性换算 `pkt` 的 `pts/dts/duration` |
| `av_inv_q(q)` | 取倒数，`av_inv_q(24/1) = 1/24` |
| `av_q2d(q)` | 有理数转 double |

#### 3.3 一条数据在链路上的「单位变化史」

这是本项目最容易搞错的地方，先背下来：

| 阶段 | 数据 | pts 所属时间基 |
| --- | --- | --- |
| `av_read_frame` 读出的 pkt | `AVPacket` | **输入流**的 `st->time_base`（例：1/12288） |
| 送进解码器再取出的 frame | `AVFrame` | 与输入 pkt 相同（解码器原样透传） |
| 送给编码器的 frame | `AVFrame` | **编码器**的 `enc_ctx->time_base`（本项目 = 1/framerate） |
| 编码器吐出的 pkt | `AVPacket` | 同上，= `enc_ctx->time_base` |
| 写进容器前的 pkt | `AVPacket` | **写头之后**的 `st->time_base` |
| 音频 pkt | `AVPacket` | 输入音频流 tb → 输出音频流 tb（直接 remux） |

> 只要某一行的「实际单位」和「代码以为的单位」不一致，时间轴就废了。

#### 3.4 MP4 会改写你的 time_base

`avformat_write_header()` **可能修改** `st->time_base`。MP4 muxer 为了保证时间精度，会把 video track 的 timescale 抬到 ≥ 10000：

```text
设进去 1/24 → 写头后变成 1/12288（24 × 512）
```

所以：**写包时必须用写头之后的 `st->time_base`**，不能用写头前自己设的值。这一条坑让 10 秒的片子变成了 0.018 秒。

---

## 第二部分：FFmpeg 函数速查

### 四、解封装流程

```text
avformat_open_input              打开文件 + 分配 AVFormatContext
        ↓
avformat_find_stream_info        真正读一段数据，探测流参数（宽高/帧率/采样率）
        ↓
av_dump_format                   打印流信息（调试用，is_output=0）
        ↓
avcodec_parameters_to_context    AVCodecParameters → AVCodecContext，准备解码
        ↓
av_seek_frame                    （可选）跳到 begin 秒
        ↓
av_read_frame                    循环读 AVPacket，一次一个包
```

| 函数 | 说明与坑 |
| --- | --- |
| `avformat_open_input(&ctx, url, NULL, NULL)` | 传 `NULL` 表示自动探测格式 |
| `avformat_find_stream_info(ctx, NULL)` | 不调用就拿不到 `avg_frame_rate`、`extradata` 等，很多源直接解不出来 |
| `av_read_frame(ctx, pkt)` | 内部会先 `av_packet_unref(pkt)`，但仍需自己 `av_packet_alloc/free`。返回 `AVERROR_EOF` 是正常结束，不是错误 |
| `av_seek_frame(ctx, stream_index, ts, flags)` | `ts` 的单位是**该流的 time_base**。带 `AVSEEK_FLAG_BACKWARD` 会落到 `ts` 之前最近的关键帧，所以**实际起点会早于 begin 秒** |
| `avcodec_parameters_to_context(cctx, par)` | 拷贝描述性参数（extradata 会深拷贝），不含运行状态 |

### 五、封装流程

```text
avformat_alloc_output_context2    按 url 后缀决定容器格式（.mp4 → mov/mp4）
        ↓
avformat_new_stream × N           建输出流，设置 codecpar
avcodec_parameters_from_context   ★ 视频流参数必须来自【编码器】
        ↓
avio_open                         打开 IO（文件 / 网络 / 管道）
        ↓
avformat_write_header             ★ 可能改写 st->time_base；失败必须终止
        ↓
av_interleaved_write_frame        循环写包，内部按 dts 缓冲排序
        ↓
av_interleaved_write_frame(NULL)  冲刷交错缓冲
av_write_trailer                  ★ MP4 的 moov 索引在这一步才落盘
        ↓
avio_closep + avformat_free_context
```

| 函数 | 说明与坑 |
| --- | --- |
| `avformat_alloc_output_context2(&c, NULL, NULL, url)` | 不指定格式，靠 url 后缀推断 |
| `avformat_new_stream(c, NULL)` | 返回的 `AVStream` 需要自己填 `codecpar` 和 `time_base` |
| `avcodec_parameters_from_context(par, enc_ctx)` | **视频输出流必须用它**（参数来自编码器）。用输入流的 `codecpar` 就是「容器声明 H.264、实际装 HEVC」的灾难 |
| `avio_open(&c->pb, url, AVIO_FLAG_WRITE)` | 少这一步写头会失败 |
| `avformat_write_header(c, NULL)` | 见 3.4。失败后 **不能**再 write/trailer，否则段错误 |
| `av_interleaved_write_frame(c, pkt)` | 会**接管 pkt 的数据引用**（内部 unref），但 `AVPacket` 结构体仍要 `av_packet_free`。它按 dts 做跨流排序，所以音频必须真的写进去，否则视频排不出来 |
| `av_interleaved_write_frame(c, NULL)` | 冲刷交错缓冲，必须在 trailer 前调用 |
| `av_write_trailer(c)` | 非流式容器（mp4/mov）的文件索引在这里才写，漏了就得到不可播放的文件 |

### 六、解码 / 缩放 / 编码

三者都是「send → receive」模型，**必须循环 receive 直到 EAGAIN**：

```c
avcodec_send_packet(c, pkt);          // 送
while (avcodec_receive_frame(c, f) == 0) { /* 收 */ }   // 收到 EAGAIN 为止
```

| 函数 | 说明与坑 |
| --- | --- |
| `avcodec_find_decoder / _encoder(id)` | 找不到返回 `NULL` |
| `avcodec_alloc_context3(codec)` | 只分配，不含参数 |
| `avcodec_open2(c, codec, NULL)` | 真正初始化，参数必须在之前设好 |
| `avcodec_send_packet` / `receive_frame` | `EAGAIN = 队列还没腾空`，不是错误 |
| `avcodec_send_frame` / `receive_packet` | 同上。**`send` 返回 `EAGAIN` 时若直接 `return`，这一帧就被静默丢掉** |
| `avcodec_send_packet(NULL)` / `send_frame(NULL)` | **冲刷**。不送 `NULL`，编解码器内部缓冲（B 帧重排、多线程延迟）里的尾部数据永远拿不出来 |
| `sws_getCachedContext` | 参数不变就复用，变了自动重建；用 `sws_ctx = sws_getCachedContext(sws_ctx, ...)` 自赋值 |
| `sws_scale(ctx, src_data, src_ls, 0, h, dst_data, dst_ls)` | 调用前必须 `av_frame_get_buffer` 给目标帧分配内存 |

---

## 第三部分：本项目实现

### 七、项目结构与类设计

```text
125_record_remux/
├── 125_record_remux.cpp   # main：串起 解封装→解码→缩放→编码→封装 + 截断
├── xformat.h/.cpp         # 基类：上下文生命周期、参数复制、时间基换算、流索引映射
├── xdemux.h/.cpp          # 解封装：open / read / seek / eof
├── xmux.h/.cpp            # 封装：建输出上下文 / 写头 / 写包 / 写 trailer
├── xcodec.h/.cpp          # 编解码基类：Create / Open / CreateFrame / SetOpt
├── xdecode.h/.cpp         # 解码：Send / Recv / End(冲刷) / initHW
├── xencode.h/.cpp         # 编码：Encode / End(冲刷)
├── xsws.h/.cpp            # 缩放：Scale（自动管理 SwsContext + 目标帧缓冲）
└── CMakeLists.txt
```

| 类 | 持有 | 关键接口 |
| --- | --- | --- |
| `XFormat`（基类） | `AVFormatContext *c_` + `std::mutex` | `set_c` `CopyPara` `RescaleTime` `SetStreamMap` `OutStreamIndex` |
| `XDemux` | 继承 `XFormat` | `Open` `Read` `Seek` `eof` |
| `XMux` | 继承 `XFormat` | `Open` `WriteHeader` `Write` `WriteEnd` |
| `XCodec`（基类） | `AVCodecContext *c_` | `Create` `Open` `set_c` `CreateFrame` `SetOpt` |
| `XDecode` | 继承 `XCodec` | `Send` `Recv` `End` `initHW` |
| `XEncode` | 继承 `XCodec` | `Encode` `End` |
| `XSws` | `SwsContext *sws_ctx_` | `Scale`（内部缓存上下文 + 按需重建目标帧） |

设计要点：

- **上下文所有权下放**：`set_c(ptr)` 之后由类负责释放，调用方不再碰原指针。
- **基类持有锁**：`XFormat` 的每个公开方法都先 `unique_lock`，便于后续改造成多线程流水线。
- **索引映射**：输入流的 `stream_index` 与输出流不保证一致，`SetStreamMap` 建立 `输入索引 → 输出索引`，`RescaleTime` 内部自动改写成输出索引。
- **无 `any`、无裸指针逃逸**：`XEncode::Encode` 返回 `std::vector<AVPacket*>`，所有权明确交给调用方 `av_packet_free`。

### 八、main 的六个阶段

#### 阶段 1：解封装 + 解码器就绪

```cpp
XDemux demux;
AVFormatContext *demux_c = XDemux::Open(in_file.c_str());   // open + find_stream_info
demux.set_c(demux_c);

// 纯音频输入在这里被挡住（否则 streams[-1] 越界）
if (demux.video_index() < 0) { ... return -1; }

XDecode de;
auto dec_ctx = XDecode::Create(demux.video_codec_id(), false);
demux.CopyPara(demux.video_index(), dec_ctx);   // codecpar → AVCodecContext
de.set_c(dec_ctx);
de.Open();
auto frame = de.CreateFrame();
```

#### 阶段 2：编码器就绪（核心三行）

```cpp
XEncode en;
auto enc_ctx = XEncode::Create(AV_CODEC_ID_HEVC, true);     // 统一 H.265

enc_ctx->framerate = demux_c->streams[demux.video_index()]->avg_frame_rate;
if (enc_ctx->framerate.num <= 0 || enc_ctx->framerate.den <= 0)
  enc_ctx->framerate = {25, 1};              // 0/0 兜底
enc_ctx->time_base = av_inv_q(enc_ctx->framerate);   // ★ 必须 1/framerate
enc_ctx->pix_fmt   = AV_PIX_FMT_YUV420P;
enc_ctx->width     = video_width;
enc_ctx->height    = video_height;
en.Open();
```

`time_base` 与 `framerate` 不一致，是转码时间轴错位的头号来源。

#### 阶段 3：封装器就绪

```cpp
AVFormatContext *mux_c = XMux::Open(out_file.c_str(),
                                    demux.video_index() >= 0,
                                    demux.audio_index() >= 0);
mux.set_c(mux_c);

// 视频：参数来自【编码器】
auto mvs = mux_c->streams[mux.video_index()];
mvs->time_base = enc_ctx->time_base;
avcodec_parameters_from_context(mvs->codecpar, enc_ctx);   // ★ 不是 demux.CopyPara
mux.SetStreamMap(demux.video_index(), mux.video_index());

// 音频：不解码，参数直接搬输入
auto mas = mux_c->streams[mux.audio_index()];
mas->time_base = {demux.audio_time_base().num, demux.audio_time_base().den};
demux.CopyPara(demux.audio_index(), mas->codecpar);
mux.SetStreamMap(demux.audio_index(), mux.audio_index());

mux.WriteHeader();

// 写头可能改写 time_base，必须用写头之后的值
const AVRational video_out_tb = mux_c->streams[mux.video_index()]->time_base;
```

#### 阶段 4：截断点与 Seek

```cpp
// 时间基倒数 = 1 秒对应多少 pts
double t = (double)demux.video_time_base().den / demux.video_time_base().num;
video_begin_pts = begin_sec * t;
if (end_sec > 0) video_end_pts = end_sec * t;
if (begin_sec > 0) demux.Seek(video_begin_pts, demux.video_index());
```

音频的 `begin/end` 用**音频流的 time_base** 单独算，不要复用视频的。

#### 阶段 5：主循环

三条流各走各的路，视频那条是完整转码链：

```text
读 pkt
 ├─ 视频：超过 end → 结束整个循环
 │        Send → while(Recv) → [frame.pts < begin 就丢] → encode_frame()
 └─ 音频：超过 end → 丢弃；pts < begin → 丢弃
          否则 RescaleTime（输入 tb → 输出 tb，并减 begin 偏移）→ Write
```

缩、编、写三件事收在一个 lambda 里，保证**全程只用编码器时间基**：

```cpp
long long enc_frame_index{0};        // 单调帧序号，单位 = enc_ctx->time_base

auto encode_frame = [&](const AVFrame *src) {
  if (!sws.Scale(src, scaled_frame, enc_ctx->width, enc_ctx->height,
                 enc_ctx->pix_fmt))
    return;                          // 缩放失败就丢帧，不复用上一帧的残留数据

  scaled_frame->pts = enc_frame_index++;      // ★ 不给解码帧的原 pts

  for (auto out : en.Encode(scaled_frame)) {
    out->stream_index = mux.video_index();
    av_packet_rescale_ts(out, enc_ctx->time_base, video_out_tb);   // ★ 编码 tb → 封装 tb
    mux.Write(out);
    av_packet_free(&out);
  }
};
```

#### 阶段 6：双端冲刷 + 收尾

```cpp
// 解码器：不冲刷会丢掉尾部 has_b_frames + 线程数 帧（实测 17 帧 ≈ 0.7 秒）
for (auto tail : de.End()) {
  if (tail->pts == AV_NOPTS_VALUE || tail->pts >= video_begin_pts)
    encode_frame(tail);
  av_frame_free(&tail);
}

// 编码器：吐出 B 帧重排缓冲里的剩余包
for (auto out : en.End()) {
  out->stream_index = mux.video_index();
  av_packet_rescale_ts(out, enc_ctx->time_base, video_out_tb);
  mux.Write(out);
  av_packet_free(&out);
}

mux.WriteEnd();       // av_interleaved_write_frame(NULL) + av_write_trailer
```

---

## 第四部分：调试实录

### 九、「音频正常、视频花屏」背后的 8 个坑

现象：程序跑完不报错，播放时音频完美、视频花屏/黑屏/卡死。

用 `ffprobe` 看输出文件，第一个坑立刻现形：

```bash
$ ffprobe out.mp4
  codec_name=h264   codec_tag=avc1   width=1920  height=1080     ← 声明 H.264 / 源分辨率
$ ffmpeg -i out.mp4 -f null -
  [h264] Invalid NAL unit size (17564159 > 30918)
  [h264] non-existing PPS 175 referenced
  [h264] no frame!                                                ← 逐帧解码失败
```

#### 坑 1（致命）：输出流 codecpar 从输入文件复制

```cpp
// ✗ 改前：把源的 H.264 参数（含 avcC extradata、源宽高）当成输出流参数
demux.CopyPara(demux.video_index(), mvs->codecpar);

// ✓ 改后：参数必须来自编码器
avcodec_parameters_from_context(mvs->codecpar, enc_ctx);
```

容器说「我是 H.264/1920×1080」，实际装的是 H.265/1280×720。解码器按 avcC 解析 HEVC 的 NAL，每一帧都是垃圾 → 花屏。

**音频为什么没事**：音频不解码不编码，`codecpar` 从输入复制恰好就是对的。

#### 坑 2：编码器 time_base 写死 1/25

`XCodec::Create` 里 `c->time_base = {1,25}` 只是占位默认值，转码源如果是 24fps 就全错。

```cpp
// ✓ 必须先按帧率覆盖
enc_ctx->time_base = av_inv_q(enc_ctx->framerate);
```

#### 坑 3：begin 偏移被减了两次

```cpp
// ✗ 改前
mux.RescaleTime(pkt, video_begin_pts, demux.video_time_base());   // 解码前减一次
... 编码后 ...
mux.RescaleTime(pkt, video_begin_pts, demux.video_time_base());   // 又减一次
```

实测输出视频包 `pts` 从 `-73728`（约 -6 秒）开始，`dts` 也全负。播放器按负时间戳排帧，直接卡住。

#### 坑 4：编码器输出的包用输入流时间基换算

编码器吐出的 pkt 时间基是 `enc_ctx->time_base`，却拿 `demux.video_time_base()` 当输入时间基做 `RescaleTime`，换算系数完全错。

```cpp
// ✓
av_packet_rescale_ts(out, enc_ctx->time_base, video_out_tb);
```

#### 坑 5：解码帧的 pts 直接喂给编码器

解码帧 `pts` 属于输入流时间基（1/12288，每帧步进 512），编码器却按 1/24 解释它 —— 每帧被当成「过了 512/24 ≈ 21 秒」。

```cpp
// ✓ 给编码器一个纯粹的帧序号
scaled_frame->pts = enc_frame_index++;
```

#### 坑 6：写头之后 time_base 变了

`avformat_write_header` 把 `st->time_base` 从 `1/24` 改成 `1/12288`，而代码还在按 1/24 写包 → **10 秒的片子 duration 变成 0.018 秒**，224 帧全挤在几毫秒里。

```cpp
// ✓ 写头后重新取
const AVRational video_out_tb = mux_c->streams[mux.video_index()]->time_base;
```

#### 坑 7：不冲刷解码器

`XDecode::End()` 写好了却从没被调用。解码器（多线程 + B 帧）内部缓存 17 帧，不送 `NULL` 就永远取不出来。

实测：241 个视频包只写出 224 帧，尾部 0.7 秒凭空消失。

#### 坑 8：begin 之前的包整体丢弃

`RescaleTime` 的写法是「pts 减去 begin」，于是 begin 之前的包变成负 pts。有人会改成「pts < begin 就丢包」——**结果把 Seek 落点那个关键帧一起丢了**，后面整个 GOP 的 P/B 帧都参考不到。

实测：`[5,10]` 段 121 个包只解出 80 帧（少了 41 帧）。

```cpp
// ✓ 包全部送进解码器，只在【解码后】按 frame->pts 丢画面
if (frame->pts != AV_NOPTS_VALUE && frame->pts < video_begin_pts)
  continue;
encode_frame(frame);
```

#### 附带修掉的 4 个问题

| 问题 | 后果 | 修复 |
| --- | --- | --- |
| `send_frame` 返回 `EAGAIN` 时直接 `return` | 该帧静默丢失，画面跳帧 | 先 `drain` 收空输出队列，再 send，再 drain |
| 用 `pkt->stream_index == mux.video_index()` 判断流类型 | `RescaleTime` 已把它换成输出索引，只有输入/输出索引恰好相同时才碰巧对 | 直接用 `is_video` / `is_audio` 局部 bool |
| `for (auto pkt : epkt)` 遮蔽外层 `pkt` | `free` 后 `epkt` 留悬垂指针；`break` 只跳出内层 | 改用独立变量名，写完立刻 `free` |
| `XMux::Open` 无条件建两条流 | 纯视频源多一条 `codec_id=NONE` 的音频流，`write_header` 报 `sample rate not set` | 按输入实际存在的流建输出流 |

### 十、验证结果

测试源：H.264 / 1280×720 / 24fps + AAC，15 秒。

```text
输入: ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=24:duration=15 \
             -f lavfi -i sine=frequency=440:duration=15 \
             -c:v libx264 -c:a aac test_src.mp4

$ ./125_record_remux test_src.mp4 out.mp4 0 10 640 360
  → hevc 640x360, 241 帧, duration=10.000000, 音频 9.961s

$ ./125_record_remux test_src.mp4 out.mp4 5 10 640 360
  → hevc 640x360, 121 帧, duration= 5.000000, 音频 4.946s

$ ./125_record_remux test_src.mp4 out.mp4 0 0 640 360      # 全片
  → hevc 640x360, 360 帧, duration=14.958333, 音频 15.000s

纯视频源（无音频）
  → hevc 160x120, 100 帧, 3.96s（不再写头失败）
```

| 检查项 | 结果 |
| --- | --- |
| `ffmpeg -i out.mp4 -f null -` | 零报错（改前每帧 `no frame!`） |
| 视频起始时间 | `start_time = 0.000000`（改前 -6 秒） |
| 帧数完整性 | 10 秒 = 241 帧，15 秒 = 360 帧，一帧不丢 |
| 画面正确性 | 与原片缩放版 PSNR ≈ 28 dB（CRF 28 有损转码的正常水平，不存在错位/绿屏） |

### 十一、编译运行

```bash
cd src/125_record_remux/build
cmake ..
cmake --build .

./125_record_remux input.mp4 output.mp4 [起始秒] [结束秒] [宽] [高]
```

依赖：FFmpeg 4.3（libavformat 58.45 / libavcodec 58.91）+ x265，链接 `avformat avcodec avutil swscale swresample`。

### 十二、已知限制与可继续改进

- **只处理单路视频 + 单路音频**，多音轨/字幕/数据流直接跳过。
- **音频不做重采样、不重新编码**，直接 remux。若源是 Opus 而目标是 mp4，兼容性一般。
- **截断点落在关键帧之前**时，从关键帧开始解码、丢掉不要的画面，这是必然的时间精度损失（最大一个 GOP）；音频按包切，误差 < 1 帧。
- **串行处理**，解码/编码没用多线程流水线（类里已经预留了 `std::mutex`）。
- 可以把 `sws_scale` 换成 `libavfilter`（`buffer` → `scale` → `buffersink`），后续接 deinterlace/rotate/overlay 更方便。
- 只支持软解，`XDecode::initHW` 已经写好但 main 没调用；开硬解后 `XSws` 前的帧格式会变（NV12 等），要同步改 `enc_ctx->pix_fmt`。
