#include "xmux.h"
#include <iostream>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

AVFormatContext *XMux::Open(const char *url, bool with_video,
                          bool with_audio) {
  AVFormatContext *c = nullptr;
  // 创建上下文
  // 直接通过 url推断格式
  auto re = avformat_alloc_output_context2(&c, NULL, NULL, url);
  BERR(re);

  // 添加 视频流和音频流
  // 改前问题：这里无条件 new 两条流。输入是纯视频时，音频流的
  // codecpar->codec_id 还是 AV_CODEC_ID_NONE，avformat_write_header
  // 会直接失败（"sample rate not set"），纯音频源同理。
  if (with_video) {
    auto vs = avformat_new_stream(c, NULL);
    if (!vs) {
      avformat_free_context(c);
      return nullptr;
    }
    vs->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
  }

  if (with_audio) {
    auto as = avformat_new_stream(c, NULL);
    if (!as) {
      avformat_free_context(c);
      return nullptr;
    }
    as->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
  }

  // 改前问题：没检查 nb_streams，两条流都不建时会得到一个空容器
  // 去写头，同样是失败。这里提前报错。
  if (c->nb_streams == 0) {
    std::cerr << "输出上下文没有可用流" << std::endl;
    avformat_free_context(c);
    return nullptr;
  }

  // 打开 IO
  // url 写入文件
  re = avio_open(&c->pb, url, AVIO_FLAG_WRITE);
  if (re < 0) {
    PrintErr(re);
    avformat_free_context(c);
    return nullptr;
  }

  return c;
}

bool XMux::WriteHeader() {
  std::unique_lock<std::mutex> lock(mux_);
  if (!c_)
    return false;

  auto re = avformat_write_header(c_, NULL);
  BERR(re);

  header_written_ = true;

  // 打印输出上下文
  av_dump_format(c_, 0, c_->url, 1);
  // is_output 为1
  return true;
}

bool XMux::Write(AVPacket *pkt) {
  std::unique_lock<std::mutex> lock(mux_);
  if (!c_ || !header_written_)
    return false;
  // 写入一帧数据，内部缓冲排序 pts, 通过 pkt = null 可以写入缓冲
  auto re = av_interleaved_write_frame(c_, pkt);
  BERR(re);

  return true;
}

bool XMux::WriteEnd() {
  std::unique_lock<std::mutex> lock(mux_);
  if (!c_ || !header_written_)
    return false;
  av_interleaved_write_frame(c_, NULL);

  // 写入尾部 trailer
  auto re = av_write_trailer(c_);
  BERR(re);
  return true;
}
