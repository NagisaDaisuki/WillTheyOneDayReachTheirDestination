#include "xvideoview.h"
#include "xopengl.h"
#include "xsdl.h"

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
