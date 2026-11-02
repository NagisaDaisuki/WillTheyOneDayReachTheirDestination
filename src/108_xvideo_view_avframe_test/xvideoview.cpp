#include "xvideoview.h"
// #include "xopengl.h"
#include "xsdl.h"
extern "C" {
#include <libavcodec/avcodec.h>
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
