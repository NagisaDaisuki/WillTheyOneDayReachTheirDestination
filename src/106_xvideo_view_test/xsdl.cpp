#include "xsdl.h"
#include <SDL2/SDL.h>
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
  SDL_Event ev;
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

bool XSDL::Present() {
  if (scale_w_ > 0 && scale_h_ > 0) {
    // 仅独立窗口模式（Scale 显式指定大小时）才去改窗口尺寸；
    // 嵌入外部窗口时窗口大小由宿主控件管理，不能动
    int ww, wh;
    SDL_GetWindowSize(win_, &ww, &wh); // 缓存值
    if (ww != scale_w_ || wh != scale_h_)
      SDL_SetWindowSize(win_, scale_w_, scale_h_); // 同步 + 触发 view更新
  }

  // 清空屏幕
  SDL_RenderClear(render_);

  // 目标矩形传 NULL：由 Init 中设置的 SDL_RenderSetLogicalSize
  // 自动完成等比缩放 + 居中 + 黑边（letterbox），窗口任意缩放都不变形
  int dst_w = (scale_w_ > 0) ? scale_w_ : width_;
  int dst_h = (scale_h_ > 0) ? scale_h_ : height_;
  SDL_Rect rect{0, 0, dst_w, dst_h};
  auto re = SDL_RenderCopy(render_, texture_, NULL, &rect);
  if (re != 0) {
    std::cout << SDL_GetError() << std::endl;
    SDL_RenderPresent(render_);
    return false;
  }

  SDL_RenderPresent(render_);
  return true;
}

bool XSDL::Init(int w, int h, PixFormat fmt, void *win_id) {
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
    if (!win_id) {
      // 不带 SDL_WINDOW_OPENGL：交由 SDL_CreateRenderer 自行选择后端
      win_ =
          SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                           w, h, SDL_WINDOW_RESIZABLE);
    } else {
      // 渲染到控件窗口（仅 X11/XWayland 下可用，Wayland 原生下会失败）
      win_ = SDL_CreateWindowFrom(win_id);
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
    sdl_fmt = SDL_PIXELFORMAT_RGBA8888;
    break;
  case XVideoView::ARGB:
    sdl_fmt = SDL_PIXELFORMAT_ARGB8888;
    break;
  case XVideoView::YUV420P:
    sdl_fmt = SDL_PIXELFORMAT_IYUV;
    break;
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
  if (!FixLinesize(&linesize))
    return false;

  // 复制内存到显存
  auto re = SDL_UpdateTexture(texture_, NULL, data, linesize);
  if (re != 0) {
    std::cout << SDL_GetError() << std::endl;
    return false;
  }

  return Present();
}

bool XSDL::Draw(const unsigned char *y, int y_linesize, const unsigned char *u,
                int u_linesize, const unsigned char *v, int v_linesize) {
  if (!y || !u || !v)
    return false;

  std::unique_lock<std::mutex> sdl_lock(mtx_);
  if (!texture_ || !render_ || !win_ || width_ <= 0 || height_ <= 0) {
    return false;
  }
  if (fmt_ != XVideoView::YUV420P)
    return false;

  // 各平面 linesize 缺省时按 YUV420P 布局推算
  if (y_linesize <= 0)
    y_linesize = width_;
  if (u_linesize <= 0)
    u_linesize = width_ / 2;
  if (v_linesize <= 0)
    v_linesize = width_ / 2;

  // 三平面分别上传，兼容 AVFrame 中平面分离 / 有对齐 padding 的情况
  auto re = SDL_UpdateYUVTexture(texture_, NULL, y, y_linesize, u, u_linesize,
                                 v, v_linesize);
  if (re != 0) {
    std::cout << SDL_GetError() << std::endl;
    return false;
  }

  return Present();
}
