#pragma once
#include <chrono>
#include <cstdint>

struct AVFrame;

/////////////////////////////////////////////////////////
//// Linux 零拷贝渲染器
//// VAAPI 解码 surface -> dma_buf -> EGLImage -> GL 纹理 -> 屏幕
//// 全程不经过 CPU 内存，没有 av_hwframe_transfer_data
////
//// 与 119（下载到内存再 SDL_UpdateTexture）的区别只在渲染这一层，
//// 解码部分完全一样
/////////////////////////////////////////////////////////
class GLVideoView {
public:
  GLVideoView() = default;
  ~GLVideoView();

  GLVideoView(const GLVideoView &) = delete;
  GLVideoView &operator=(const GLVideoView &) = delete;

  /////////////////////////////////////////
  //// 创建窗口 + GL(EGL) 上下文 + shader
  //// @para w/h 视频的可见宽高（窗口初始大小）
  //// @return 是否成功
  /////////////////////////////////////////
  bool Init(int w, int h);

  /////////////////////////////////////////
  //// 渲染一帧 AV_PIX_FMT_VAAPI 的 AVFrame
  //// 内部通过 vaExportSurfaceHandle 把 surface 导出成 dma_buf，
  //// 再用 EGL 导入成 GL 纹理，直接采样上屏
  /////////////////////////////////////////
  bool DrawFrame(AVFrame *frame);

  // 处理窗口事件，返回 true 表示用户要求退出
  bool IsExit();

  void Close();

  int render_fps() const { return render_fps_; }
  int width() const { return vis_w_; }
  int height() const { return vis_h_; }

private:
  bool InitGL();
  bool InitShaders();
  void DestroyImages();
  void Stats();

  unsigned int tex_y_{0};  // Y  平面纹理（R8）
  unsigned int tex_uv_{0}; // UV 平面纹理（GR88，UV 交错）

  void *win_{nullptr}; // SDL_Window*
  void *gl_ctx_{nullptr};
  void *egl_dpy_{nullptr}; // EGLDisplay

  // EGL 扩展函数指针（EGL 1.4 之后靠 eglGetProcAddress 取）
  void *fn_create_image_{nullptr};
  void *fn_destroy_image_{nullptr};
  void *fn_image_target_{nullptr};

  unsigned int prog_{0};
  unsigned int vao_{0};
  unsigned int vbo_{0};
  int uni_tex_y_{-1};
  int uni_tex_uv_{-1};
  int uni_uv_scale_{-1};

  uintptr_t img_y_{0};  // EGLImage
  uintptr_t img_uv_{0};
  uintptr_t pending_y_{0}; // 上一帧的 EGLImage，等这一帧开始时再销毁
  uintptr_t pending_uv_{0};

  int vis_w_{0};  // 可见尺寸（视频真实宽高）
  int vis_h_{0};
  int surf_w_{0}; // surface 尺寸（驱动按对齐补齐，通常高度对齐到 32）
  int surf_h_{0};

  int render_fps_{0};
  int count_{0};
  std::chrono::steady_clock::time_point beg_time_{};
  bool ok_{false};
};
