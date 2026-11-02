#include "xopengl.h"

#include <QEvent>
#include <QWidget>
#include <cstring>
#include <iostream>

namespace {
// 顶点着色器：全屏三角形带，vUV 翻转 Y（图像自顶向下，GL 纹理自底向上）
const char *kVertSrc = R"(
attribute vec2 aPos;
varying vec2 vUV;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUV = vec2(aPos.x * 0.5 + 0.5, 0.5 - aPos.y * 0.5);
}
)";

// 片元着色器：uYuv=1 时做 YUV420P -> RGB（BT.601），否则直接采样 RGBA
const char *kFragSrc = R"(
uniform sampler2D uTexY;
uniform sampler2D uTexU;
uniform sampler2D uTexV;
uniform sampler2D uTexRGBA;
uniform int uYuv;
varying vec2 vUV;
void main() {
    if (uYuv == 1) {
        float y = texture2D(uTexY, vUV).r;
        float u = texture2D(uTexU, vUV).r - 0.5;
        float v = texture2D(uTexV, vUV).r - 0.5;
        float r = y + 1.402 * v;
        float g = y - 0.344136 * u - 0.714136 * v;
        float b = y + 1.772 * u;
        gl_FragColor = vec4(r, g, b, 1.0);
    } else {
        gl_FragColor = texture2D(uTexRGBA, vUV);
    }
}
)";
} // namespace

XOpenGL::XOpenGL(QWidget *parent) : QOpenGLWidget(parent) {
  // 请求兼容Profile 的 GL 2.1：保证 texture2D/客户端顶点属性等
  // 老式 GLSL 在各平台驱动下都可用
  QSurfaceFormat fmt;
  fmt.setVersion(2, 1);
  fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
  setFormat(fmt);
}

bool XOpenGL::CopyPlane(const unsigned char *src, int stride, int w, int h,
                        std::vector<unsigned char> &dst) {
  if (!src || stride <= 0 || w <= 0 || h <= 0)
    return false;
  dst.resize(static_cast<size_t>(w) * h);
  for (int row = 0; row < h; ++row) {
    memcpy(&dst[static_cast<size_t>(row) * w], src + static_cast<size_t>(row) * stride, w);
  }
  return true;
}

bool XOpenGL::Init(int w, int h, PixFormat fmt, void *win_id) {
  if (w <= 0 || h <= 0)
    return false;

  std::unique_lock<std::mutex> lock(mtx_);
  width_ = w;
  height_ = h;
  fmt_ = fmt;

  // 按像素格式分配帧缓冲（打包、无 padding）
  switch (fmt) {
  case XVideoView::YUV420P:
    y_buf_.assign(static_cast<size_t>(w) * h, 0);
    u_buf_.assign(static_cast<size_t>(w) * h / 4, 0);
    v_buf_.assign(static_cast<size_t>(w) * h / 4, 0);
    rgba_buf_.clear();
    break;
  case XVideoView::RGBA:
  case XVideoView::ARGB:
    rgba_buf_.assign(static_cast<size_t>(w) * h * 4, 0);
    y_buf_.clear();
    u_buf_.clear();
    v_buf_.clear();
    break;
  default:
    return false;
  }
  frame_dirty_ = true;

  // 嵌入宿主控件：作为子控件铺满，并跟随宿主 resize
  // （Wayland 下不使用任何窗口句柄嵌入，规避 SDL_CreateWindowFrom 的平台限制）
  if (win_id) {
    auto *host = static_cast<QWidget *>(win_id);
    host->installEventFilter(this);
    setParent(host);
    setGeometry(0, 0, host->width(), host->height());
  }
  show();
  return true;
}

bool XOpenGL::Draw(const unsigned char *data, int linesize) {
  if (!data)
    return false;

  std::unique_lock<std::mutex> lock(mtx_);
  if (width_ <= 0 || height_ <= 0)
    return false;

  switch (fmt_) {
  case XVideoView::YUV420P: {
    if (linesize <= 0)
      linesize = width_;
    const int cw = width_ / 2, ch = height_ / 2;
    // IYUV 连续内存：Y 平面之后依次是 U、V 平面
    if (!CopyPlane(data, linesize, width_, height_, y_buf_))
      return false;
    if (!CopyPlane(data + static_cast<size_t>(width_) * height_, linesize / 2,
                   cw, ch, u_buf_))
      return false;
    if (!CopyPlane(data + static_cast<size_t>(width_) * height_ +
                       static_cast<size_t>(cw) * ch,
                   linesize / 2, cw, ch, v_buf_))
      return false;
    break;
  }
  case XVideoView::RGBA:
  case XVideoView::ARGB: {
    if (linesize <= 0)
      linesize = width_ * 4;
    if (!CopyPlane(data, linesize, width_ * 4, height_, rgba_buf_))
      return false;
    break;
  }
  default:
    return false;
  }

  frame_dirty_ = true;
  // 数据已拷入内部缓冲，请求 GUI 线程重绘（实际上传+绘制在 paintGL）
  update();
  return true;
}

bool XOpenGL::Draw(const unsigned char *y, int y_linesize,
                   const unsigned char *u, int u_linesize,
                   const unsigned char *v, int v_linesize) {
  if (!y || !u || !v)
    return false;

  std::unique_lock<std::mutex> lock(mtx_);
  if (width_ <= 0 || height_ <= 0 || fmt_ != XVideoView::YUV420P)
    return false;

  if (y_linesize <= 0)
    y_linesize = width_;
  if (u_linesize <= 0)
    u_linesize = width_ / 2;
  if (v_linesize <= 0)
    v_linesize = width_ / 2;

  if (!CopyPlane(y, y_linesize, width_, height_, y_buf_))
    return false;
  if (!CopyPlane(u, u_linesize, width_ / 2, height_ / 2, u_buf_))
    return false;
  if (!CopyPlane(v, v_linesize, width_ / 2, height_ / 2, v_buf_))
    return false;

  frame_dirty_ = true;
  update();
  return true;
}

void XOpenGL::initializeGL() {
  initializeOpenGLFunctions();

  program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertSrc);
  program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragSrc);
  program_.bindAttributeLocation("aPos", 0);
  if (!program_.link()) {
    std::cerr << "XOpenGL shader link failed" << std::endl;
    return;
  }

  // 全屏四边形（三角形带）
  static const GLfloat quad[] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
  vbo_.create();
  vbo_.bind();
  vbo_.allocate(quad, static_cast<int>(sizeof(quad)));

  glGenTextures(4, tex_);
  for (unsigned int t : tex_) {
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  program_.bind();
  program_.setUniformValue("uTexY", 0);
  program_.setUniformValue("uTexU", 1);
  program_.setUniformValue("uTexV", 2);
  program_.setUniformValue("uTexRGBA", 3);
  program_.release();
  glBindTexture(GL_TEXTURE_2D, 0);

  gl_ready_ = true;
}

void XOpenGL::UploadPlane(int tex_index, int w, int h,
                          const std::vector<unsigned char> &buf) {
  if (buf.size() < static_cast<size_t>(w) * h)
    return;
  glActiveTexture(GL_TEXTURE0 + tex_index);
  glBindTexture(GL_TEXTURE_2D, tex_[tex_index]);
  // GL_LUMINANCE 在 GL 2.1/兼容 Profile 下即为单通道纹理
  glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE,
               GL_UNSIGNED_BYTE, buf.data());
}

void XOpenGL::paintGL() {
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

  std::unique_lock<std::mutex> lock(mtx_);
  if (!gl_ready_ || width_ <= 0 || height_ <= 0)
    return;

  // 上传最新帧
  switch (fmt_) {
  case XVideoView::YUV420P:
    UploadPlane(0, width_, height_, y_buf_);
    UploadPlane(1, width_ / 2, height_ / 2, u_buf_);
    UploadPlane(2, width_ / 2, height_ / 2, v_buf_);
    break;
  case XVideoView::RGBA:
  case XVideoView::ARGB:
    if (rgba_buf_.size() >= static_cast<size_t>(width_) * height_ * 4) {
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D, tex_[3]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, rgba_buf_.data());
    }
    break;
  default:
    return;
  }

  // Letterbox：按视频纵横比在控件（设备像素）内计算最大居中视口，
  // 修复画面拉伸变形；Hi-DPI 由 devicePixelRatioF 换算
  const qreal dpr = devicePixelRatioF();
  const int win_w = qRound(width() * dpr);
  const int win_h = qRound(height() * dpr);
  int dst_w = win_w, dst_h = win_h;
  const double src_ar = static_cast<double>(width_) / height_;
  const double win_ar = static_cast<double>(win_w) / win_h;
  if (win_ar > src_ar) {
    dst_w = qRound(win_h * src_ar);
  } else {
    dst_h = qRound(win_w / src_ar);
  }
  glViewport((win_w - dst_w) / 2, (win_h - dst_h) / 2, dst_w, dst_h);
  glClear(GL_COLOR_BUFFER_BIT);

  program_.bind();
  program_.setUniformValue("uYuv", fmt_ == XVideoView::YUV420P ? 1 : 0);

  vbo_.bind();
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(0);
  vbo_.release();
  program_.release();
}

bool XOpenGL::eventFilter(QObject *watched, QEvent *event) {
  // 宿主控件 resize 时让 GL 子控件继续铺满宿主
  if (watched == parent() && event->type() == QEvent::Resize) {
    auto *host = static_cast<QWidget *>(watched);
    setGeometry(0, 0, host->width(), host->height());
  }
  return QOpenGLWidget::eventFilter(watched, event);
}
