#include "xformat.h"
#include <iostream>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

void PrintErr(int err) {
  char buf[1024] = {0};
  av_strerror(err, buf, sizeof(buf) - 1);
  std::cerr << buf << std::endl;
}

bool XFormat::CopyPara(int stream_index, AVCodecParameters *dst) {
  std::unique_lock<std::mutex> lock(mux_);
  if (!c_) // 上下文没有设定
    return false;
  // 判断索引是否正确，必须是 >= c_->nb_streams 等于也会越界
  if (stream_index < 0 || stream_index >= c_->nb_streams)
    return false;

  auto re = avcodec_parameters_copy(dst, c_->streams[stream_index]->codecpar);
  if (re < 0)
    return false;
  return true;
}
bool XFormat::CopyPara(int stream_index, AVCodecContext *dst) {
  std::unique_lock<std::mutex> lock(mux_);
  if (!c_) // 上下文没有设定
    return false;

  if (stream_index < 0 || stream_index >= c_->nb_streams)
    return false;
  auto re =
      avcodec_parameters_to_context(dst, c_->streams[stream_index]->codecpar);
  if (re < 0)
    return false;
  return true;
}

void XFormat::set_c(AVFormatContext *c) {
  std::unique_lock<std::mutex> lock(mux_);
  if (c_) {            // 清理原值
    if (c_->iformat) { // 如果是输入上下文
      avformat_close_input(&c_);
    } else if (c_->oformat) { // 如果是输出上下文
      if (c_->pb)
        avio_closep(&c_->pb);
      avformat_free_context(c_);
    } else {
      avformat_close_input(&c_);
    }
  }
  c_ = c; // 直接赋值

  // 用于区分是否有视频或音频流
  // 在流创建的时候一定要设置流参数类型(AVMEDIA_TYPE_VIDEO/AUDIO)
  // 无论是否释放资源都要重置，否则空上下文会残留上一个上下文的值，
  // 调用者拿 video_index() 去索引 streams 就会越界/用错流
  audio_index_ = -1;
  video_index_ = -1;
  stream_map_.clear();

  // 如果是资源释放
  if (!c_)
    return;

  for (int i = 0; i < c->nb_streams; ++i) {
    // 视频
    if (c->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_index_ = i;
      video_time_base_ = {c->streams[i]->time_base.num,
                          c->streams[i]->time_base.den}; // 记录真实时间基
      video_codec_id_ = c->streams[i]->codecpar->codec_id;
    }
    // 音频
    if (c->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audio_index_ = i;
      audio_time_base_ = {c->streams[i]->time_base.num,
                          c->streams[i]->time_base.den}; // 记录真实时间基
    }
  }
}

bool XFormat::RescaleTime(AVPacket *pkt, long long offset_pts,
                          XRational time_base) {
  std::unique_lock<std::mutex> lock(mux_);
  if (!c_)
    return false;

  // pkt->stream_index 是输入流索引，先换算成输出流索引
  int out_index = OutStreamIndex(pkt->stream_index);
  if (out_index < 0)
    return false;

  auto out_stream = c_->streams[out_index];
  AVRational in_time_base = {time_base.num, time_base.den};

  // 1. 处理 PTS
  if (pkt->pts != AV_NOPTS_VALUE)
    pkt->pts = av_rescale_q_rnd(
        pkt->pts - offset_pts, in_time_base, out_stream->time_base,
        (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

  // 2. 处理DTS
  if (pkt->dts != AV_NOPTS_VALUE)
    pkt->dts = av_rescale_q_rnd(
        pkt->dts - offset_pts, in_time_base, out_stream->time_base,
        (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

  // 3. 如果 DTS 无效，尝试使用 PTS补偿
  if (pkt->dts == AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE)
    pkt->dts = pkt->pts;

  // 4. 如果 PTS 无效，尝试使用 DTS补偿
  if (pkt->pts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE)
    pkt->pts = pkt->dts;

  pkt->duration =
      av_rescale_q(pkt->duration, in_time_base, out_stream->time_base);
  pkt->pos = -1;
  // 回写输出流索引，后续 Write() 才能写进正确的流
  pkt->stream_index = out_index;

  return true;
}

void XFormat::SetStreamMap(int in_index, int out_index) {
  std::unique_lock<std::mutex> lock(mux_);
  if (in_index < 0 || out_index < 0)
    return;
  stream_map_[in_index] = out_index;
}

int XFormat::OutStreamIndex(int in_index) const {
  if (in_index < 0 || !c_)
    return -1;

  int out_index = in_index;
  auto it = stream_map_.find(in_index);
  if (it != stream_map_.end())
    out_index = it->second;

  if (out_index < 0 || out_index >= (int)c_->nb_streams)
    return -1;
  return out_index;
}
