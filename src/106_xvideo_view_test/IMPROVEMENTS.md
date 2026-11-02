---
title: XVideoView YUV 播放器改进总结
publishedAt: 2026-10-23
description: 对 106_xvideo_view_test 的 XVideoView 接口与 SDL/OpenGL 双渲染后端进行的全面改进：Wayland 支持、窗口缩放修复、资源泄漏治理与 FFmpeg 兼容性
tags: [AI, C++, Qt, SDL, FFmpeg, OpenGL, Wayland]
category: 视频开发
draft: false
---

# XVideoView YUV 播放器改进总结

本文记录对 `106_xvideo_view_test`（Qt + SDL 的 YUV420P 裸流播放 Demo）一轮完整改进的内容，涉及 7 个源文件与 CMake 配置，全部改动已构建验证通过。

## 一、本次改进全景

| # | 改进项 | 文件 | 解决的问题 |
|---|--------|------|-----------|
| 1 | 接口层新增虚析构 + 三平面 `Draw` 重载 | `xvideoview.h` | 基类指针释放不安全、无法对接 AVFrame |
| 2 | XSDL 重复 Init 资源泄漏、软件渲染回退、逻辑尺寸缩放、线性采样 | `xsdl.h` / `xsdl.cpp` | 泄漏、黑屏无回退、画面拉伸变形、缩放锯齿 |
| 3 | 新增 XOpenGL 渲染后端（QOpenGLWidget + YUV shader） | `xopengl.h` / `xopengl.cpp` | **Wayland 无法播放**、嵌入缩放、Hi-DPI |
| 4 | demo 层成员化、检查 Init 返回值、EOF 半帧修复、销毁顺序 | `sdlplayyuv.h` / `sdlplayyuv.cpp` | 静态全局变量、静默失败、闪烁、生命周期错乱 |
| 5 | CMake 接入 Qt OpenGL 组件与新增源文件 | `CMakeLists.txt` | XOpenGL 后端编译链接 |

## 二、接口层改进（xvideoview.h）

### 1. 基类虚析构

原接口没有虚析构函数，通过 `XVideoView*` 指针 `delete` 派生类对象（如 `XSDL`、`XOpenGL`）是未定义行为。新增：

```cpp
virtual ~XVideoView() = default;
```

### 2. 三平面 Draw 重载

原 `Draw(data, linesize)` 只接受单指针，隐含要求 YUV420P 数据是 IYUV 连续内存。FFmpeg 解码出的 `AVFrame` 是 `data[0..2]` 三个独立平面、各自带对齐 padding 的 `linesize`，原接口无法正确对接。新增：

```cpp
virtual bool Draw(const unsigned char *y, int y_linesize,
                  const unsigned char *u, int u_linesize,
                  const unsigned char *v, int v_linesize) = 0;
```

两个后端均实现了该重载；U/V `linesize` 缺省时按 `width/2` 推算。

## 三、XSDL 后端修复（xsdl.cpp）

### 1. 重复 Init 不再泄漏

原代码第二次调用 `Init()` 会直接再创建 renderer / texture，旧对象全部泄漏。现在重建前先销毁：

```cpp
if (texture_) { SDL_DestroyTexture(texture_); texture_ = nullptr; }
if (render_)  { SDL_DestroyRenderer(render_); render_ = nullptr; }
```

窗口只创建一次并复用。

### 2. 加速渲染器失败时回退软件渲染

原来只申请 `SDL_RENDERER_ACCELERATED`，失败即黑屏。现在回退 `SDL_RENDERER_SOFTWARE`。

### 3. 用逻辑分辨率修复缩放变形（SDL 窗口缩放问题核心修复）

原实现每帧 `SDL_GetRendererOutputSize` 拿目标尺寸，直接把纹理拉伸到满屏——label 不是 4:3 时画面被拉扁。改为：

```cpp
SDL_RenderSetLogicalSize(render_, w, h);   // Init 时一次设置
SDL_RenderCopy(render_, texture_, NULL, NULL);  // Draw 时目标矩形传 NULL
```

SDL 自动完成 **等比缩放 + 居中 + 黑边（letterbox）**，窗口任意缩放都不变形，还顺带删掉了每帧手工计算目标矩形的代码。

### 4. 线性采样消除缩放锯齿

SDL2 纹理默认 nearest 采样，480×360 放大到 1024×768 会出现块状伪影。新增：

```cpp
SDL_SetTextureScaleMode(texture_, SDL_ScaleModeLinear);
```

### 5. 其他

- 去掉了无意义的 `SDL_WINDOW_OPENGL` 窗口标志（避免强制选择 GL 后端）；
- `linesize` 缺省推算、清屏/绘制/上屏逻辑抽取为 `FixLinesize()` / `Present()` 私有辅助函数，单平面与三平面两条 `Draw` 路径共用。

## 四、新增 XOpenGL 后端（Wayland 支持核心）

### 背景

原方案靠 `SDL_CreateWindowFrom(label->winId())` 把 SDL 渲染嵌进 Qt 控件。**Wayland 协议没有外部窗口嵌入的概念**，`CreateWindowFrom` 在 Wayland 原生会话下必然失败（返回 NULL），表现为静默黑屏。这是原程序在 Wayland 上不能播放的根因。

### 方案

新增 `XOpenGL : QOpenGLWidget + XVideoView`，利用接口层预留的 `OPENGL` 枚举：

- **渲染**：在 `paintGL` 中用 shader 完成 YUV420P → RGB（BT.601），三平面分别上传为 `GL_LUMINANCE` 纹理；RGBA/ARGB 单纹理直传。请求 GL 2.1 Compatibility Profile 保证老式 GLSL 全平台可用。
- **嵌入**：`Init` 时把自身作为宿主 `QWidget` 的子控件铺满，并通过 `eventFilter` 跟随宿主 resize——完全不碰窗口句柄，Wayland / X11 / Windows 通用。
- **缩放**：`paintGL` 中按视频纵横比计算居中 letterbox 视口（`glViewport`），修复拉伸变形，且自带线性采样。
- **Hi-DPI**：由 `QOpenGLWidget` 的 framebuffer + `devicePixelRatioF()` 换算自动处理，删除了原 demo 中算了没用的 `dpr` 死代码。
- **线程模型**：`Draw` 只把帧数据拷入内部缓冲并调 `update()`，真正的上传与绘制发生在 GUI 线程的 `paintGL`，由 `mtx_` 保护，符合"线程安全"的接口约定。

### 后端自动选择

demo 侧按运行时平台选择后端：

```cpp
XVideoView::RenderType type = XVideoView::SDL;
if (QGuiApplication::platformName().contains("wayland"))
  type = XVideoView::OPENGL;
```

同时区分两种后端的 `win_id` 语义：SDL 后端传 X11 Window 句柄（`label->winId()`），OPENGL 后端传宿主 `QWidget*`（`label`）。

## 五、demo 层改进（sdlplayyuv.cpp）

1. **静态全局变量全部成员化**：`view / yuv / yuv_file / sdl_width / sdl_height` 变为 `SdlPlayYuv` 成员，`view` / `yuv` 用 `std::unique_ptr` 管理，多实例安全。
2. **检查 `Init()` 返回值**：创建或初始化失败时弹 `QMessageBox` 提示，不再静默黑屏（原来 XWayland 下嵌入失败就是这种表现）。
3. **EOF 半帧修复**：读到 EOF 时原来会用残留的半帧旧数据画一次导致闪烁；现在 `seekg(0)` 后**重读完整一帧再绘制**。
4. **销毁顺序修正**：析构时依次 `view_.reset()`（先清理 GL/SDL 渲染资源）→ 关闭文件 → `SDL_Quit()` → `delete ui`，修复了原来"先 `SDL_Quit` 后泄漏 view"的错乱。
5. `timerEvent` 增加防御性判空。

## 六、CMake 改动

- `PROJECT_SOURCES` 加入 `xopengl.cpp / xopengl.h`；
- Qt6 链接 `Qt6::OpenGLWidgets`（Qt5 回退 `Qt5::OpenGL`）；
- 其余 SDL2 / Qt Widgets 链接不变。

## 七、验证结果

- `cmake --build build` 全量编译链接通过；
- LSP 全量诊断 8 个源文件 0 error；
- 程序可正常启动（缺 yuv 文件时正确弹出提示框）。

## 八、遗留与后续方向

1. **色彩空间**：YUV shader 目前固定 BT.601；接 FFmpeg 解码后应按 `AVFrame->colorspace` 切换 601/709 与 full/limited range（SDL 路径可用 `SDL_HINT_RENDER_YUV_CONFORMANCE`）。
2. **帧率控制**：33ms Qt 定时器仍有抖动，可换 `QElapsedTimer` 校准或对接解码线程 + 音频时钟同步。
3. **XWayland 下的 SDL 路径**：强制 `QT_QPA_PLATFORM=xcb` 时 SDL 嵌入路径可用，但 Wayland 原生会话已统一走 XOpenGL，无需再依赖嵌入。
4. **DIRECTX 后端**：枚举仍返回 `nullptr`，Windows 平台可按同样模式补充。
