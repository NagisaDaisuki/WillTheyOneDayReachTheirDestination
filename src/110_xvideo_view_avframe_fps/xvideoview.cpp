#include "xvideoview.h"
// #include "xopengl.h"
#include "xsdl.h"
extern "C" {
#include <libavcodec/avcodec.h>
}
#include <thread>

void MSleep(unsigned long ms) {
  auto beg = std::chrono::steady_clock::now();
  for (int i = 0; i < ms; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - beg)
            .count() >= ms)
      break;
  }
}

XVideoView *XVideoView::Create(RenderType type) {
  switch (type) {
  case XVideoView::SDL:
    return new XSDL();
  case XVideoView::OPENGL:
    // return new XOpenGL();
  default:
    break;
  }

  return nullptr;
}

void XVideoView::Scale(int w, int h) {
  scale_w_ = w;
  scale_h_ = h;
}

bool XVideoView::DrawFrame(AVFrame *frame) {
  if (!frame || !frame->data[0])
    return false;
  // 计算一秒显示帧率
  count_++;
  // 获取当前时间点
  auto now = std::chrono::steady_clock::now();

  // 如果 beg_time_ 是默认值（刚初始化，还没记录过）
  // 可以用 time_since_epoch().count == 0 来判断
  if (beg_time_.time_since_epoch().count() == 0) {
    beg_time_ = now;
  }
  // 如果距离上次记录，已经过去了 >= 1秒
  else if (now - beg_time_ >= std::chrono::seconds(1)) {
    render_fps_ = count_;
    count_ = 0;
    beg_time_ = now;
  }

  switch (frame->format) {
  case AV_PIX_FMT_YUV420P:
    return Draw(frame->data[0], frame->linesize[0], frame->data[1],
                frame->linesize[1], frame->data[2], frame->linesize[2]);
    break;
  case AV_PIX_FMT_BGRA:
    return Draw(frame->data[0], frame->linesize[0]);
    break;
  default:
    break;
  }
  return true;
}
