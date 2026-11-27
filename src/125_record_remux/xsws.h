#pragma once
#include <libavutil/pixfmt.h>
#include <mutex>

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

class XSws {
public:
  XSws() = default;
  ~XSws();

  // 禁止拷贝
  XSws(const XSws &) = delete;
  XSws &operator=(const XSws &) = delete;

  //////////////////////////////////////////////////////////////
  /// 缩放并转换格式
  //////////////////////////////////////////////////////////////
  bool Scale(const AVFrame *in_frame, AVFrame *out_frame, int dst_w, int dst_h,
             AVPixelFormat dst_fmt);

private:
  SwsContext *sws_ctx_{nullptr};

  // 记录上一次的输入参数，用于判断还是否需要重新创建上下文
  int last_src_w_{0};
  int last_src_h_{0};
  AVPixelFormat last_src_fmt_{AV_PIX_FMT_NONE};

  std::mutex mtx_;
};
