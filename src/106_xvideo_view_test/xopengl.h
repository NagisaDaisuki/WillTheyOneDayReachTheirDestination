#pragma once
#include "xvideoview.h"

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <vector>

////////////////////////////////////////////
//// OpenGL 渲染后端（QOpenGLWidget + YUV shader）
////
//// 使用 Qt 原生 GL 控件渲染，不再依赖 SDL_CreateWindowFrom
//// 的窗口嵌入，因此 Wayland / X11 / Windows 全平台可用。
////
//// win_id 语义：宿主 QWidget 指针，本控件作为其子控件铺满；
//// 缩放由 paintGL 中按纵横比计算的 letterbox 视口完成，
//// Hi-DPI 由 QOpenGLWidget 的 framebuffer 自动处理。
////////////////////////////////////////////
class XOpenGL : public QOpenGLWidget, protected QOpenGLFunctions, public XVideoView {
public:
  explicit XOpenGL(QWidget *parent = nullptr);

  /////////////////////////////////////////
  //// 初始化渲染 线程安全
  //// @para w/h 视频分辨率（决定纹理大小，与控件大小无关）
  //// @para fmt 像素格式（支持 YUV420P / RGBA / ARGB）
  //// @para win_id 宿主 QWidget 指针；为空则创建独立顶层窗口
  //// @return 是否创建成功
  /////////////////////////////////////////
  bool Init(int w, int h, PixFormat fmt = RGBA,
            void *win_id = nullptr) override;

  /////////////////////////////////////////
  //// 渲染图像 线程安全
  //// YUV420P 要求 IYUV 连续内存（Y 平面后接 U、V 平面）
  /////////////////////////////////////////
  bool Draw(const unsigned char *data, int linesize = 0) override;

  /////////////////////////////////////////
  //// 三平面渲染重载 线程安全（FFmpeg AVFrame 兼容）
  /////////////////////////////////////////
  bool Draw(const unsigned char *y, int y_linesize,
            const unsigned char *u, int u_linesize,
            const unsigned char *v, int v_linesize) override;

protected:
  void initializeGL() override;
  void paintGL() override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  // 拷贝一个平面到打包缓冲（按源 stride 逐行拷贝，消除对齐 padding）
  static bool CopyPlane(const unsigned char *src, int stride, int w, int h,
                        std::vector<unsigned char> &dst);

  // 把指定缓冲上传为单通道（YUV）纹理
  void UploadPlane(int tex_index, int w, int h,
                   const std::vector<unsigned char> &buf);

  QOpenGLShaderProgram program_;
  QOpenGLBuffer vbo_;
  unsigned int tex_[4] = {0, 0, 0, 0}; // 0=Y 1=U 2=V 3=RGBA
  bool gl_ready_ = false;

  // Draw 线程写入、paintGL 读取的帧缓冲（由 mtx_ 保护）
  std::vector<unsigned char> y_buf_, u_buf_, v_buf_, rgba_buf_;
  bool frame_dirty_ = false;
};
