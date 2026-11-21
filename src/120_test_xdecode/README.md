---
title: h264 裸流解码播放器项目总结：缩放实现与踩坑记录
publishedAt: 2026-11-21
description: 基于 FFmpeg + SDL2 的 h264 裸流解码播放小项目总结，重点剖析 SDL 纹理拉伸式缩放的完整实现链路，以及循环播放、double free、缩放失效等实际踩坑记录。
tags: [AI, FFmpeg, SDL2, H264, 视频解码, C++]
category: 音视频开发
draft: false
---

# h264 裸流解码播放器项目总结

## 一、项目概述

一个小型的 h264 裸流（`setokai.h264`）解码播放 Demo，核心链路：

```
h264 裸流文件 → av_parser 分帧 → XDecode 软解 → AVFrame(YUV420P) → XVideoView/XSDL 渲染
```

工程文件结构：

| 文件 | 职责 |
|---|---|
| `120_test_xdecode.cpp` | main 流程：读文件、分帧、解码循环、渲染循环 |
| `xcodec.h/.cpp` | 编解码基类：持有 `AVCodecContext`，封装 `Create / SetOpt / Open / CreateFrame`，互斥锁保证线程安全 |
| `xdecode.h/.cpp` | 解码子类：`Send / Recv / End(flush) / initHW`，软解直接产出软件帧，硬解路径内部做显存→内存拷贝 |
| `xencode.h/.cpp` | 编码子类（本项目未用到，接口预留） |
| `xvideoview.h/.cpp` | 渲染抽象基类：`DrawFrame` 按 `frame->format` 分发到不同的 `Draw` 重载 |
| `xsdl.h/.cpp` | SDL2 渲染实现：窗口 / 渲染器 / 纹理生命周期 + 缩放绘制 |
| `xvideoview.cpp` | 另含 `ReadFile`（读裸 YUV/RGB 文件测试渲染）与 fps 统计 |

## 二、解码流程要点

1. **分帧**：`av_parser_parse2` 按 4096 字节喂入裸流，靠起始码 `0001` 切出完整 NAL 填充进 `AVPacket`。
2. **解码**：`avcodec_send_packet` / `avcodec_receive_frame` 经典异步模型，每 Send 一次就 drain 一轮 Recv，避免 EAGAIN 堵塞。
3. **循环播放**：读到文件尾 `ifs.clear() + seekg(0)` 回卷，`while (!view->IsExit())` 保证点窗口关闭可退出。
4. **fps 统计**：每 100ms 打一次 `count * 10`。
5. **结束 flush**：退出循环后 `XDecode::End()` 发送空 packet 洗出解码器缓冲帧并逐个绘制释放。

类设计上，`XCodec` 析构自动 `avcodec_free_context`，`set_c` 传入的上下文由对象接管生命周期，是典型的 RAII 包装。

## 三、重点：缩放的实现剖析

### 3.1 总体方案

采用的方案是 **"纹理保持原始分辨率 + SDL_RenderCopy 拉伸到目标矩形"**，即缩放不在像素搬运阶段做，而在 GPU 光栅化阶段由 SDL 完成：

```
解码帧 → UpdateTexture(原尺寸像素进纹理)
              ↓
窗口真实大小(SDL_GetWindowSize) → FitRect 等比算出居中矩形
              ↓
SDL_RenderCopy(整张纹理 → 该矩形)   ← 唯一发生缩放的地方
              ↓
SDL_RenderPresent
```

### 3.2 分步拆解（全部位于 `xsdl.cpp`）

**① 纹理永远是视频原始尺寸（Init）**

```cpp
texture_ = SDL_CreateTexture(render_, sdl_fmt, SDL_TEXTUREACCESS_STREAMING, w, h);
```

纹理大小 = 解码帧宽高，**不随窗口变化**，整个播放期间只存"原始画面"。

**② 每帧像素搬运进纹理（Draw）**

```cpp
SDL_UpdateYUVTexture(texture_, NULL, y, y_pitch, u, u_pitch, v, v_pitch);
```

这一步是逐字节 memcpy 级别的搬运，`pitch/linesize` 只负责行对齐，没有任何缩放参与。

**③ Present() 确定目标区域**

```cpp
int dst_w = width_, dst_h = height_;
if (win_id_) {                                  // 嵌入模式：用宿主给的 Scale() 值
  if (scale_w_ > 0) dst_w = scale_w_;
  if (scale_h_ > 0) dst_h = scale_h_;
} else if (win_) {
  SDL_GetWindowSize(win_, &dst_w, &dst_h);      // 独立窗口：取真实窗口大小
}
```

关键设计：独立窗口模式**不依赖事件**（SDL 收不到外部 X11 窗口 resize 的场景也安全），每帧直接查当前窗口大小；嵌入模式才走 `Scale()` 手动指定。随后按需重设视口并用 `view_w_/view_h_` 缓存避免重复调用。

**④ FitRect()：等比缩放的核心，纯整数运算**

```cpp
const long long by_w = dst_w * height_;   // dst_w/width_ 与
const long long by_h = dst_h * width_;    // dst_h/height_ 交叉相乘比较
if (by_w <= by_h)
  h = by_w / width_;    // 宽度先撞边 → 按宽度铺满
else
  w = by_h / height_;   // 高度先撞边 → 按高度铺满
```

两个设计点：

- 用**交叉相乘比较两个分数**，避免浮点除法的精度误差；
- 结果 `(dst_w-w)/2, (dst_h-h)/2` 居中，剩余区域留黑边，`max(w,1)` 防止极小窗口退化成 0 尺寸。

**⑤ 黑边判断 + 拉伸绘制**

```cpp
const bool covers_all = dst[0]==0 && dst[1]==0 && dst[2]==dst_w && dst[3]==dst_h;
if (!covers_all) SDL_RenderClear(render_);  // 有黑边才清屏

SDL_Rect rect{dst[0], dst[1], dst[2], dst[3]};
SDL_RenderCopy(render_, texture_, nullptr, &rect);  // 缩放发生在这里
```

源矩形传 `nullptr` 表示"整张纹理"，目标矩形传 `rect`，SDL 光栅化时把纹理**采样拉伸**到 rect 大小。铺满时跳过清屏省一次 GPU 操作。

**⑥ 插值质量 hint**

```cpp
SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");  // 1=双线性插值，0=最近邻
```

只影响第⑤步拉伸时的采样方式（放大是否平滑），不是缩放开关。

### 3.3 与 SDL_RenderSetLogicalSize 的取舍

`Init` 里注释掉的 `SDL_RenderSetLogicalSize` 是官方等价方案（逻辑坐标系自动 letterbox），但被刻意弃用，原因：

1. **嵌入模式失效**：嵌入（`SDL_CreateWindowFrom`）下 SDL 收不到外部 X11 窗口的 resize 事件，逻辑分辨率不会跟随窗口变化；
2. **黑边不可控**：LogicalSize 自带 letterbox，无法做到无黑边铺满。

"手动取窗口大小 + FitRect" 等价于自己实现了 LogicalSize 的行为，且对嵌入模式完全可控。

## 四、踩坑记录

### 坑 1：缩放设置无效 —— hint 被当成了缩放开关

**现象**：Linux 上拖动窗口，画面不跟随缩放。

**错误认知**：以为 `SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1")` 设置了缩放。

**真相**：该 hint 只是**插值算法**（缩放发生时用线性插值防锯齿），不触发缩放本身。而旧版 `Present()` 中 `dst_w/dst_h` 取自 `scale_w_/scale_h_`——这两个值只有 `Scale()` 会赋值，main 从未调用，于是绘制矩形恒等于视频原始尺寸，根本没有发生拉伸。

**修复**：独立窗口模式下改用 `SDL_GetWindowSize` 取真实窗口大小作为目标区域（见 3.2 第③步），配合已有的 FitRect，窗口拖动画面实时等比跟随。

### 坑 2：循环播放写法导致主循环永不退出

**现象**：`decode.End()` 与所有释放代码永远执行不到，程序只能强杀。

**原因**：

```cpp
while (!ifs.eof()) {
  ifs.read(...);
  if (ifs.eof()) {
    ifs.clear();               // 每次都把 eof 标志清掉
    ifs.seekg(0, std::ios::beg);
  }
}
```

回卷后循环顶部 `ifs.eof()` 永远为 false → 无限循环。

**修复**：循环条件改为 `while (!view->IsExit())`，保留回卷逻辑实现循环播放，点窗口关闭正常退出并执行 flush。

### 坑 3：double free 与资源泄漏

**现象**：退出代码里 `frame` 被释放两次，`hw_frame` 从未释放：

```cpp
// 释放 hw_frame
av_frame_free(&frame);   // ← 注释写 hw_frame，实际释放的还是 frame
// 释放 frame
av_frame_free(&frame);   // ← double free，退出时 abort
```

**修复**：删掉多余的一次 free；未使用的 `hw_frame` 直接移除（硬解路径中 `XDecode` 内部会自己分配临时帧）。

### 其他防御性细节（写对的点）

- `ReadPlane` 逐行读取跳过 linesize 对齐填充，避免像素错位；
- `ReadFile` 中 `av_frame_get_buffer` 的 align 取值兼顾"整帧一次读完"与 SDL GL 后端对 pitch 整除字节数的要求，避免 RGB24 斜切；
- `IsExit` 中 `SDL_Event ev{}` 零初始化，防止超时未写事件时读到脏数据；
- `XVideoView` 提供虚析构，保证基类指针 delete 派生类资源正确释放。

## 五、可选改进方向

- `XDecode::Send` 失败直接丢数据，流式场景应 EAGAIN 时先 drain 再重试；
- `XVideoView::Create()` 可能返回 nullptr，main 中应判空；
- 解码速度即渲染速度，无帧率节流，如需按原始帧率播放可在 Draw 后按 `AVCodecContext` 的 `time_base` 做 `MSleep` 定时。
