#include "xencode.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

///////////////////////////////////////////////
/// 把编码器里已经编码完成的包全部取出
/// 必须先清空输出队列再 send_frame，否则 send 会返回 EAGAIN
///////////////////////////////////////////////
static void drain_packets(AVCodecContext *c, std::vector<AVPacket *> &out) {
  for (;;) {
    auto pkt = av_packet_alloc();
    if (!pkt)
      return;
    auto re = avcodec_receive_packet(c, pkt);
    if (re == 0) {
      out.push_back(pkt); // 成功收到包，塞入数组
      continue;
    }
    av_packet_free(&pkt); // 没数据，销毁空盒子
    if (re != AVERROR(EAGAIN) && re != AVERROR_EOF)
      PrintErr(re);
    return;
  }
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

  // 1. 先把已经编码好的包全部收出来（腾空输出队列）
  // 改前问题：直接 send_frame，一旦编码器输出队列还没被收空，
  // send 会返回 AVERROR(EAGAIN)，而原来对 EAGAIN 的处理是
  // “return 空 vector”，这一帧就被静默丢掉了（画面跳帧），
  // 程序也不报错。EAGAIN 不是错误，是本帧没送进去。
  drain_packets(c_, resultPkts);

  // 2. 送数据（队列已腾空，正常不会再 EAGAIN）
  auto re = avcodec_send_frame(c_, frame);
  if (re < 0) {
    PrintErr(re);
    return resultPkts;
  }

  // 3. 取出本次编码出的包
  drain_packets(c_, resultPkts);
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

  // 1. 先把残留包收干净，避免 send NULL 拿到 EAGAIN 后
  //    尾部数据永远出不来（改前直接用 while + break 处理，
  //    EAGAIN 也没问题，但为了一致性统一走 drain）
  drain_packets(c_, resultPkts);

  // 2. 发送 NULL 冲刷
  avcodec_send_frame(c_, NULL);

  // 3. 循环接收残余数据
  drain_packets(c_, resultPkts);
  return resultPkts;
}
