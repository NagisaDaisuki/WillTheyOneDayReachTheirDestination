#include "xdemux.h"
extern "C" {
#include <libavformat/avformat.h>
}

AVFormatContext *XDemux::Open(const char *url) {
  AVFormatContext *c = nullptr;

  // 打开解封装上下文
  auto re = avformat_open_input(&c, url, NULL, NULL);
  BERR(re);

  // 获取媒体信息
  re = avformat_find_stream_info(c, NULL);
  BERR(re);

  // 打印输入封装信息
  av_dump_format(c, 0, url, 0); // 设定输出上下文 0

  return c;
}

bool XDemux::Read(AVPacket *pkt) {
  std::unique_lock<std::mutex> lock(mux_);
  if (!c_)
    return false;

  auto re = av_read_frame(c_, pkt);
  // 读到结尾是正常情况，不能当错误报
  if (re == AVERROR_EOF) {
    eof_ = true;
    return false;
  }
  // 其他负值才是真正的读取错误
  if (re < 0) {
    PrintErr(re);
    return false;
  }
  eof_ = false;
  return true;
}

bool XDemux::Seek(long long pts, int stream_index) {
  std::unique_lock<std::mutex> lock(mux_);
  if (!c_)
    return false;
  auto re = av_seek_frame(c_, stream_index, pts,
                          AVSEEK_FLAG_FRAME | AVSEEK_FLAG_BACKWARD);
  BERR(re);
  eof_ = false; // seek 后重新判断结尾
  return true;
}
