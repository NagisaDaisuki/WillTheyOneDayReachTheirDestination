#include "xencode.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

///////////////////////////////////////////////
/// 编码数据 线程安全 每次新创建AVPacket
/// @para frame 空间由用户维护
/// @return 失败返回一个空 vector
/// 返回的 std::vector<AVPacket*>
/// av_packet_free() 清理
///////////////////////////////////////////////
std::vector<AVPacket *> XEncode::Encode(const AVFrame *frame) {
  std::vector<AVPacket *> resultPkts{};
  if (!frame)
    return resultPkts;
  std::unique_lock<std::mutex> lock(mtx_);
  if (!c_)
    return resultPkts;

  // 1. 发送数据
  auto re = avcodec_send_frame(c_, frame);
  if (re < 0 && re != AVERROR(EAGAIN)) {
    PrintErr(re);
    return resultPkts;
  }

  // 2. 循环接收（能收多少收多少）
  while (true) {
    auto pkt = av_packet_alloc();
    re = avcodec_receive_packet(c_, pkt);
    if (re == AVERROR(EAGAIN) || re == AVERROR_EOF) {
      av_packet_free(&pkt); // 没数据，销毁空盒子，退出循环
      break;
    }
    if (re < 0) {
      PrintErr(re);
      av_packet_free(&pkt);
      break;
    }

    // 成功收到包，塞入数组
    resultPkts.push_back(pkt);
  }
  return resultPkts;
}

///////////////////////////////////////////////
/// 返回所有编码缓冲中的 AVPacket
///////////////////////////////////////////////
std::vector<AVPacket *> XEncode::End() {
  std::vector<AVPacket *> resultPkts{};
  std::unique_lock<std::mutex> lock(mtx_);
  if (!c_)
    return resultPkts;

  // 1. 发送 NULL 冲刷
  avcodec_send_frame(c_, NULL);

  // 2. 循环接收残余数据
  while (true) {
    auto pkt = av_packet_alloc();
    auto re = avcodec_receive_packet(c_, pkt);
    if (re == AVERROR(EAGAIN) || re == AVERROR_EOF) {
      av_packet_free(&pkt);
      break;
    }
    if (re < 0) {
      av_packet_free(&pkt);
      break;
    }
    resultPkts.push_back(pkt);
  }
  return resultPkts;
}
