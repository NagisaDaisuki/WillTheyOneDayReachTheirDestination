#include "xsws.h"
#include <iostream>
#include <libavutil/pixfmt.h>
extern "C" {
#include <libswscale/swscale.h>
}

XSws::~XSws() {
  if (sws_ctx_) {
    sws_freeContext(sws_ctx_);
    sws_ctx_ = nullptr;
  }
}

bool XSws::Scale(const AVFrame *in_frame, AVFrame *out_frame, int dst_w,
                 int dst_h, AVPixelFormat dst_fmt) {
  if (!in_frame || !out_frame || !in_frame->data[0])
    return false;

  std::unique_lock<std::mutex> lock(mtx_);

  // 1. 如果输入参数变了，重新获取上下文
  if (sws_ctx_ == nullptr || last_src_w_ != in_frame->width ||
      last_src_h_ != in_frame->height ||
      last_src_fmt_ != (AVPixelFormat)in_frame->format) {
    sws_ctx_ =
        sws_getCachedContext(sws_ctx_, in_frame->width, in_frame->height,
                             (AVPixelFormat)in_frame->format, dst_w, dst_h,
                             dst_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
      std::cerr << "sws_getCachedContext failed!" << std::endl;
      return false;
    }

    last_src_w_ = in_frame->width;
    last_src_h_ = in_frame->height;
    last_src_fmt_ = (AVPixelFormat)in_frame->format;
  }

  // 2. 检查目标 frame 是否已经正确分配了正确大小的内存，如果没有分配
  if (out_frame->width != dst_w || out_frame->height != dst_h ||
      out_frame->format != dst_fmt) {
    if (out_frame->data[0]) {
      av_frame_unref(out_frame); // 清理旧数据
    }

    out_frame->width = dst_w;
    out_frame->height = dst_h;
    out_frame->format = dst_fmt;

    int ret = av_frame_get_buffer(out_frame, 0);
    if (ret < 0) {
      std::cerr << "failed to allocate buffer for out_frame!" << std::endl;
      return false;
    }
  }

  // 3. 执行缩放转换
  sws_scale(sws_ctx_, in_frame->data, in_frame->linesize, 0, in_frame->height,
            out_frame->data, out_frame->linesize);

  // 4. 继承时间戳
  out_frame->pts = in_frame->pts;

  return true;
}
