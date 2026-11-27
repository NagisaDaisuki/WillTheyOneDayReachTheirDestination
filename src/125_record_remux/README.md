---
title: 124_test_xformat：FFmpeg 纯转封装截断工具（含崩溃排查与修复总结）
publishedAt: 2026-11-25
description: 基于 FFmpeg 的 XFormat/XDemux/XMux 三层封装，实现音视频流不解码的转封装截断；记录一次 muxer 未初始化导致的 SIGSEGV 崩溃与系列健壮性修复。
tags: [AI, FFmpeg, C++, 封装, 调试]
category: 音视频开发
draft: false
---

# 124_test_xformat

一个基于 FFmpeg C API 的**纯转封装（remux）截断工具**：不做解码/编码，直接把输入文件的音视频 packet 搬进新容器，并按 `[start_sec, end_sec)` 截取一段。

```bash
./124_test_xformat input.mp4 output.mp4 10 30
```

## 一、项目结构

```text
124_test_xformat/
├── 124_test_xformat.cpp   # main：串起解封装 → 参数复制 → 截断 → 封装
├── xformat.h / .cpp       # 基类 XFormat：上下文管理、参数复制、时间基换算
├── xdemux.h / .cpp        # XDemux：打开输入、读帧、Seek
├── xmux.h  / .cpp         # XMux ：创建输出上下文、写头/写帧/写 trailer
└── CMakeLists.txt         # 链接 avformat / avcodec / avutil / swscale / swresample
```

### 类的关系

`XFormat` 是封装/解封装的公共基类，持有 `AVFormatContext *c_` 和一把 `std::mutex mux_`，`XDemux` / `XMux` 只负责各自特有的动作。

| 类 | 职责 | 关键接口 |
| --- | --- | --- |
| `XFormat` | 上下文生命周期、流索引探测、codecpar 复制、时间基换算、流索引映射 | `set_c()` `CopyPara()` `RescaleTime()` `SetStreamMap()` |
| `XDemux` | `avformat_open_input` → `find_stream_info` → `av_read_frame` / `av_seek_frame` | `Open()` `Read()` `Seek()` `eof()` |
| `XMux` | `avformat_alloc_output_context2` → `avformat_write_header` → `av_interleaved_write_frame` → `av_write_trailer` | `Open()` `WriteHeader()` `Write()` `WriteEnd()` |

### `set_c()` 的资源释放策略

同一个 `set_c()` 同时服务输入和输出上下文，靠 `iformat` / `oformat` 区分：

```cpp
if (c_->iformat)        avformat_close_input(&c_);            // 输入
else if (c_->oformat) { if (c_->pb) avio_closep(&c_->pb);
                        avformat_free_context(c_); }          // 输出
else                    avformat_close_input(&c_);
```

传 `nullptr` 即"关闭"，用于 `main` 结尾的清理。

## 二、主流程

```text
XDemux::Open(in)  ──┐
                    ├─► set_c() 探测 video/audio 索引与 time_base
XMux::Open(out)   ──┘
        │
        ├─► CopyPara() 复制 codecpar + 设置输出 time_base
        ├─► SetStreamMap(输入索引 → 输出索引)
        ├─► WriteHeader()
        ├─► Seek(video_begin_pts)   // 截断起点，落在起始关键帧
        └─► while (Read(pkt))
                ├─ 视频 pts > end  → 结束
                ├─ 音频 pts > end  → 丢弃该帧继续
                ├─ RescaleTime()   // 减 offset、换时间基、改 stream_index
                └─ Write(pkt)      // av_interleaved_write_frame 接管 pkt
        │
        └─► WriteEnd()  // av_write_trailer：MP4 的 moov 索引此时才落盘
```

### `RescaleTime()` 做四件事

1. `pts -= offset_pts`，再 `av_rescale_q_rnd` 到**输出流**的 time_base（`AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX`）
2. `dts` 同样处理
3. `pts` / `dts` 有一方为 `AV_NOPTS_VALUE` 时互相补偿
4. `duration` 换时间基、`pos = -1`、**回写 `pkt->stream_index` 为输出流索引**

## 三、故障复盘：为什么 SIGSEGV

### 现象

```text
Input #0 ... 1920x1080 h264 / opus 48000 Hz
[mp4 @ 0x...] sample rate not set
Invalid argument
fish: Job 1, './124_test_xformat ...' terminated by signal SIGSEGV
```

`sample rate not set` 之后没有退出，而是在 `av_write_trailer` 里崩掉：

```text
#0  ?? ()                        libavformat.so
#3  av_write_trailer ()
#4  XMux::WriteEnd  xmux.cpp:60
#5  main           124_test_xformat.cpp:135
```

### 根因：一个变量名笔误

```cpp
auto mvs = mux_c->streams[mux.video_index()];   // 视频流
auto mas = mux_c->streams[mux.audio_index()];   // 音频流

if (demux.audio_index() >= 0) {
  mvs->time_base = {...};                              // ✗ 应为 mas
  demux.CopyPara(demux.audio_index(), mvs->codecpar);  // ✗ 音频参数写进了视频流
}
```

后果链：

1. 输出音频流的 `codecpar` 完全没填（`codec_id = NONE`、`sample_rate = 0`），视频流参数反被 Opus 覆盖
2. `avformat_write_header()` 在 mov muxer 里校验失败 → `AVERROR(EINVAL)`
3. **返回值被忽略**，程序继续 `av_interleaved_write_frame` / `av_write_trailer`
4. header 失败时 mov muxer 已 `mov_free()` 掉 tracks 链表，`av_write_trailer` 解引用空 track → SIGSEGV

> 结论：`avformat_write_header` 返回错误后，muxer 内部状态是**不可用**的，任何后续写入都是未定义行为。

### 修复

```cpp
// 1. 变量用对
mas->time_base = {demux.audio_time_base().num, demux.audio_time_base().den};
demux.CopyPara(demux.audio_index(), mas->codecpar);

// 2. 头写失败立刻终止
if (!mux.WriteHeader())
  return -1;
```

```cpp
// 3. XMux 内部加状态位，从机制上杜绝"header 失败后还写帧"
private:
  bool header_written_{false};

bool XMux::Write(AVPacket *pkt) {
  if (!c_ || !header_written_) return false;
  // ...
}
bool XMux::WriteEnd() {
  if (!c_ || !header_written_) return false;
  // ...
}
```

## 四、连带修掉的健壮性问题

| 位置 | 问题 | 修法 |
| --- | --- | --- |
| `XFormat::RescaleTime` | 用**输入流索引**去索引**输出流**数组，输入/输出流顺序不同就写错流甚至越界 | 新增 `std::map<int,int> stream_map_` + `SetStreamMap()` / `OutStreamIndex()`，换算后回写 `pkt->stream_index` |
| `XFormat::set_c(nullptr)` | 提前 `return` 前没重置索引，空上下文残留 `0/1`，调用方拿它索引 `streams` 就会用错流 | `c_ == nullptr` 分支前也执行 `video_index_ = audio_index_ = -1` 并清空映射 |
| `total_sec` | `int` 类型，每帧 1/24 秒被截断成 0，结果恒为 0 | 改 `double`，且改用**输出流 time_base** 计算（原式拿输入 tb 乘已被换算过的 duration，量纲错） |
| `end_sec` | 换算写在 `if (begin_sec > 0)` 里，`0 30` 这种截断静默失效 | begin / end 各自独立换算 |
| 截断边界 | 只管视频，音频无边界检查 | 新增 `audio_end_pts`：视频到点整体结束；音频到点丢帧继续，避免视频尾部被提前截断 |
| `XDemux::Read` | `BERR(re)` 把 EOF 和真错误混为 `false` | 单独判断 `AVERROR_EOF`，新增 `eof()`，`Seek()` 复位；`main` 只在真错误时告警 |
| `XMux::Open` | 未检查 `avformat_new_stream()` / `avio_open()` 返回值，失败后解引用空指针 | 逐项判空并 `avformat_free_context()` |
| `main` | `Open()` 失败后照常往下跑 | 输入/输出打开失败立即 `return -1` |
| `main` | 字幕/数据流被送进 `RescaleTime` | 显式区分 video / audio，其余 `unref` + `continue` |

## 五、验证

环境：FFmpeg `libavformat 58.45.100`，输入 `setokai.mp4`（h264 1920x1080 + opus 48 kHz，时长 90 s）。

| 参数 | 视频帧 | 音频帧 | 容器 duration |
| --- | --- | --- | --- |
| `10 30` | 529 | 1105 | 19.989 s |
| `0 20` | 479 | 998 | 19.979 s |
| `40 60` | 490 | 1024 | 20.019 s |
| `80 200` | 253 | 531 | 10.094 s |

`ffprobe` 确认输出含 h264 + opus 双流，参数与源一致（无重编码）。

> `0 20` 是修复前会"静默不截断"的用例；`80 200` 是 end 超出文件长度，自动夹到尾部。

### 关于打印的总时长偏大

`总时长` 比容器 duration 略大（如 `10 30` 打印 22.05 s，容器报 19.99 s）是**预期行为**：

`av_seek_frame(..., AVSEEK_FLAG_BACKWARD)` 会落在 `begin_sec` 之前的关键帧上，这些 GOP 预帧确实写进了文件，但被 MP4 的 edit list 剪掉。ffmpeg CLI 同样如此，代码中已注释说明。

## 六、已知限制

1. **严格起点**：想从 `begin_sec` 的精确帧起播，需要 `avformat_seek_file` 精定位或丢弃预帧后重编码，纯转封装做不到。
2. **`pkt->stream_index` 副作用**：`RescaleTime()` 会就地改写索引，调用后不能再拿它和输入的 `demux.video_index()` 比较。
3. **`OutStreamIndex()` 不加锁**：它被 `RescaleTime()` 在持锁状态下调用（`std::mutex` 不可重入），因此约定"由调用方保证线程安全"。
4. **单写者模型**：`XFormat` 的 `mux_` 只保护上下文指针本身，不支持多线程同时读写同一上下文。

## 七、构建

```bash
mkdir -p build && cd build
cmake .. && make -j
./124_test_xformat input.mp4 output.mp4 10 30
```

依赖项目自带的 FFmpeg（`ffmpeg_project/include` + `ffmpeg_project/lib`，已设置 RPATH）。
