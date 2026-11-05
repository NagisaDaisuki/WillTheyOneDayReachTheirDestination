#include "xsdl.h"
#include "xvideoview.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <iostream>

void XSDL::Close() {
  // 确保线程安全
  std::unique_lock<std::mutex> sdl_lock(mtx_);
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }

  if (render_) {
    SDL_DestroyRenderer(render_);
    render_ = nullptr;
  }

  if (win_) {
    SDL_DestroyWindow(win_);
    win_ = nullptr;
  }
}

bool XSDL::IsExit() {
  // 注意必须零初始化：SDL_WaitEventTimeout 返回 0（超时）时不会写 ev
  SDL_Event ev{};
  SDL_WaitEventTimeout(&ev, 1);
  if (ev.type == SDL_QUIT)
    return true;
  return false;
}

static bool InitVideo() {
  static bool is_first = true;
  static std::mutex mux;
  std::unique_lock<std::mutex> sdl_lock(mux);
  if (!is_first)
    return true;
  is_first = false;
  if (SDL_Init(SDL_INIT_VIDEO)) {
    std::cout << SDL_GetError() << std::endl;
    return false;
  }
  // 设定缩放算法解决锯齿问题
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  // 1 是线性插值算法，0 是临近值插值算法
  return true;
}

#ifdef FIX_LINE_SIZE
bool XSDL::FixLinesize(int *linesize) {
  if (*linesize > 0)
    return true;
  switch (fmt_) {
  case XVideoView::RGBA:
  case XVideoView::ARGB:
    *linesize = width_ * 4;
    break;
  case XVideoView::YUV420P:
    *linesize = width_;
    break;
  }
  return *linesize > 0;
}
#endif

std::array<int, 4> XSDL::FitRect(int dst_w, int dst_h) const {
  if (width_ <= 0 || height_ <= 0 || dst_w <= 0 || dst_h <= 0)
    return {0, 0, dst_w, dst_h};

  // 交叉相乘比较 dst_w/width_ 与 dst_h/height_，避免浮点误差
  const long long by_w = static_cast<long long>(dst_w) * height_;
  const long long by_h = static_cast<long long>(dst_h) * width_;

  int w = dst_w;
  int h = dst_h;
  if (by_w <= by_h) // 宽度先到边界，按宽度铺满
    h = static_cast<int>(by_w / width_);
  else // 高度先到边界，按高度铺满
    w = static_cast<int>(by_h / height_);

  // 极小窗口下避免退化为 0 尺寸
  w = std::max(w, 1);
  h = std::max(h, 1);
  return {(dst_w - w) / 2, (dst_h - h) / 2, w, h};
}

bool XSDL::Present() {
  if (!render_ || !texture_)
    return false;

  // 目标区域：嵌入模式（win_id_ 非空）下就是宿主 label 的当前大小；
  // 未经 Scale() 指定时退回视频原始尺寸
  const int dst_w = (scale_w_ > 0) ? scale_w_ : width_;
  const int dst_h = (scale_h_ > 0) ? scale_h_ : height_;
  if (dst_w <= 0 || dst_h <= 0)
    return false;

  // 只有独立窗口模式才需要改窗口尺寸；嵌入窗口的大小由宿主控件管理，不能动
  if (!win_id_ && win_ && scale_w_ > 0 && scale_h_ > 0) {
    int ww = 0;
    int wh = 0;
    SDL_GetWindowSize(win_, &ww, &wh); // 缓存值
    if (ww != scale_w_ || wh != scale_h_)
      SDL_SetWindowSize(win_, scale_w_, scale_h_);
  }

  // 嵌入模式下 SDL 缓存的窗口尺寸不会跟随宿主控件变化（没有 SDL 事件泵），
  // 这里用宿主给出的真实大小重置视口，保证绘制坐标 1:1 落在窗口像素上
  if (view_w_ != dst_w || view_h_ != dst_h) {
    SDL_Rect vp{0, 0, dst_w, dst_h};
    SDL_RenderSetViewport(render_, &vp);
    view_w_ = dst_w;
    view_h_ = dst_h;
  }

  // 等比缩放：保持原视频宽高比放进目标区域并居中，剩余部分留黑边。
  // 铺满时可以跳过一次清屏
  const auto dst = FitRect(dst_w, dst_h);
  const bool covers_all =
      dst[0] == 0 && dst[1] == 0 && dst[2] == dst_w && dst[3] == dst_h;
  if (!covers_all)
    SDL_RenderClear(render_);

  SDL_Rect rect{dst[0], dst[1], dst[2], dst[3]};
  if (SDL_RenderCopy(render_, texture_, nullptr, &rect) != 0) {
    std::cerr << SDL_GetError() << std::endl;
    return false;
  }

  SDL_RenderPresent(render_);
  return true;
}

bool XSDL::Init(int w, int h, PixFormat fmt) {
  if (w <= 0 || h <= 0)
    return false;

  // 初始化 SDL 视频库 放在锁外
  InitVideo();

  // 确保线程安全：unique_lock 退出函数时自动 unlock
  std::unique_lock<std::mutex> sdl_lock(mtx_);

  width_ = w;
  height_ = h;
  fmt_ = fmt;

  //////////////////////////////////////////
  //// 1. 创建窗口（只创建一次，重复 Init 复用）
  //////////////////////////////////////////
  if (!win_) {
    if (!win_id_) {
      // 不带 SDL_WINDOW_OPENGL：交由 SDL_CreateRenderer 自行选择后端
      win_ =
          SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                           w, h, SDL_WINDOW_RESIZABLE);
    } else {
      // 渲染到控件窗口（仅 X11/XWayland 下可用，Wayland 原生下会失败）
      win_ = SDL_CreateWindowFrom(win_id_);
    }
  }

  if (!win_) {
    std::cerr << SDL_GetError() << std::endl;
    return false;
  }

  //////////////////////////////////////////
  //// 2. 重建渲染器（重复 Init 先销毁旧资源，避免泄漏）
  //////////////////////////////////////////
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }
  if (render_) {
    SDL_DestroyRenderer(render_);
    render_ = nullptr;
  }

  render_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED);
  if (!render_) {
    // 加速渲染器失败时回退软件渲染
    std::cerr << SDL_GetError() << std::endl;
    render_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!render_) {
    std::cerr << SDL_GetError() << std::endl;
    return false;
  }
  // 打出实际选中的后端（opengl / software），“画面不对”时先看这一行
  {
    SDL_RendererInfo info{};
    if (SDL_GetRendererInfo(render_, &info) == 0)
      std::cout << "SDL renderer: " << info.name << std::endl;
  }
  // 新建的渲染器视口是默认值，强制下一帧按宿主大小重设
  view_w_ = 0;
  view_h_ = 0;

  // 注意：这里不能用 SDL_RenderSetLogicalSize！
  // 嵌入模式（SDL_CreateWindowFrom）下 SDL 收不到外部 X11 窗口的
  // resize 事件，逻辑分辨率不会跟随窗口变化，缩放会失效；
  // 且 logical size 自带 letterbox 黑边。改用 resizeEvent 里的
  // Scale() + Present() 手动 rect 拉伸铺满（无黑边）
  // SDL_RenderSetLogicalSize(render_, w, h);

  //////////////////////////////////////////
  //// 3. 创建材质（显存）
  //////////////////////////////////////////
  unsigned int sdl_fmt = SDL_PIXELFORMAT_UNKNOWN;
  switch (fmt) {
  case XVideoView::RGBA:
    sdl_fmt = SDL_PIXELFORMAT_RGBA32;
    break;
  case XVideoView::ARGB:
    sdl_fmt = SDL_PIXELFORMAT_ARGB32;
    break;
  case XVideoView::BGRA:
    sdl_fmt = SDL_PIXELFORMAT_BGRA32;
    break;
  case XVideoView::RGB24:
    sdl_fmt = SDL_PIXELFORMAT_RGB24;
    break;
  case XVideoView::YUV420P:
    sdl_fmt = SDL_PIXELFORMAT_IYUV;
    break;
  }

  if (sdl_fmt == SDL_PIXELFORMAT_UNKNOWN) {
    std::cerr << "unsupported pixel format" << std::endl;
    return false;
  }

  texture_ =
      SDL_CreateTexture(render_,
                        sdl_fmt,                     // 像素格式
                        SDL_TEXTUREACCESS_STREAMING, // 频繁修改的渲染（带锁）
                        w, h                         // 材质大小
      );
  if (!texture_) {
    std::cerr << SDL_GetError() << std::endl;
    return false;
  }

  // 线性采样：放大时平滑，消除最近邻采样产生的锯齿/块状
  // SDL_SetTextureScaleMode(texture_, SDL_ScaleModeLinear);

  return true;
}

bool XSDL::Draw(const unsigned char *data, int linesize) {
  // 防御性编程
  if (!data)
    return false;

  // 确保线程安全
  std::unique_lock<std::mutex> sdl_lock(mtx_);
  if (!texture_ || !render_ || !win_ || width_ <= 0 || height_ <= 0) {
    return false;
  }

  // if (!FixLinesize(&linesize))
  //   return false;

  // 复制内存到显存
  auto re = SDL_UpdateTexture(texture_, NULL, data, linesize);
  if (re != 0) {
    std::cout << SDL_GetError() << std::endl;
    return false;
  }

  return Present();
}

bool XSDL::Draw(const unsigned char *y, int y_pitch, const unsigned char *u,
                int u_pitch, const unsigned char *v, int v_pitch) {

  // 防御性编程
  if (!y || !u || !v)
    return false;

  // 确保线程安全
  std::unique_lock<std::mutex> sdl_lock(mtx_);
  if (!texture_ || !render_ || !win_ || width_ <= 0 || height_ <= 0) {
    return false;
  }

  // 复制内存到显存
  // SDL_UpdateSDLTexture
  auto re =
      SDL_UpdateYUVTexture(texture_, NULL, y, y_pitch, u, u_pitch, v, v_pitch);

  if (re != 0) {
    std::cout << SDL_GetError() << std::endl;
    return false;
  }

  return Present();
}
