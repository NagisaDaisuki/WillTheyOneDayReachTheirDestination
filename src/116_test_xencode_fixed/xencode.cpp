// ============================================================================
//  xencode.cpp —— 崩溃修复快照（本文件与修复前完全一致，仅补充 [KNOWN-ISSUE] 注释）
// ============================================================================
#include "xencode.h"
#include <iostream>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

static void PrintErr(int err) {
  char buf[1024] = {0};
  av_strerror(err, buf, sizeof(buf) - 1);
  std::cerr << buf << std::endl;
}

///////////////////////////////////////////////
/// 析构函数，自动释放编码器
///////////////////////////////////////////////
XEncode::~XEncode() {
  // 析构时自动释放编码器
  if (c_) {
    avcodec_free_context(&c_);
  }
}

///////////////////////////////////////////////
/// 创建编码上下文
/// 与成员无关 static
/// @para codec_id 编码器 ID号，对应 FFmpeg
/// @return 编码上下文，失败返回 nullptr
///////////////////////////////////////////////
AVCodecContext *XEncode::Create(int codec_id) {
  // 1. 找到编码器
  auto codec = avcodec_find_encoder((AVCodecID)codec_id);
  if (!codec) {
    std::cerr << "avcodec_find_encoder failed!" << codec_id << std::endl;
    return nullptr;
  }

  // 2. 创建上下文
  AVCodecContext *c = avcodec_alloc_context3(codec);
  if (!c) {
    std::cerr << "avcodec_alloc_context3 failed!" << codec_id << std::endl;
    return nullptr;
  }

  // 3. 设置参数默认值
  // [KNOWN-ISSUE-5] 隐式时序契约，编译器无法约束：
  //   Create() 里设了 time_base/pix_fmt，却故意不设 width/height，
  //   要求调用方在 Create() 之后、Open() 之前自己填 c->width/c->height。
  //   一旦忘了，Open() 才会报错（或者更糟：到 CreateFrame() 才失败）。
  //   另外 thread_count = 16 写死，会与 x264 自己的 "threads" 选项打架。
  c->time_base = (AVRational){1, 25};
  c->pix_fmt = AV_PIX_FMT_YUV420P;
  c->thread_count = 16;

  return c;
}

///////////////////////////////////////////////
/// 设置对象的编码器上下文，上下文传递到对象中，
/// 空间由 XEncode维护 线程安全
/// @para c 编码器上下文，如果 c_ 不为 nullptr，
/// 则先清理资源
///////////////////////////////////////////////
void XEncode::set_c(AVCodecContext *c) {
  std::unique_lock<std::mutex> lock(mtx_);

  if (c_) {
    // 资源清理
    avcodec_free_context(&c_);
  }
  this->c_ = c;
}

///////////////////////////////////////////////
/// 设置编码参数，线程安全
/// C++ 支持函数重载
/// @return 是否正确添加参数
///////////////////////////////////////////////
bool XEncode::SetOpt(const char *key, const char *value) {
  std::unique_lock<std::mutex> lock(mtx_);
  if (!c_)
    return false;
  auto re = av_opt_set(c_->priv_data, key, value, 0);
  if (re != 0) {
    std::cerr << "av_opt_set failed!";
    PrintErr(re);
    return false;
  }

  return true;
}

bool XEncode::SetOpt(const char *key, int value) {
  std::unique_lock<std::mutex> lock(mtx_);
  if (!c_)
    return false;
  auto re = av_opt_set_int(c_->priv_data, key, value, 0);
  if (re != 0) {
    std::cerr << "av_opt_set_int failed!";
    PrintErr(re);
    return false;
  }
  return true;
}

///////////////////////////////////////////////
/// 打开编码器 线程安全
///////////////////////////////////////////////
bool XEncode::Open() {
  std::unique_lock<std::mutex> lock(mtx_);
  if (!c_)
    return false;
  auto re = avcodec_open2(c_, NULL, NULL);
  // AVCodecContext 在 alloc的时候就已经设置过 Codec了
  if (re != 0) {
    PrintErr(re);
    return false;
  }
  return true;
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
  // [KNOWN-ISSUE-3] 逻辑缺陷：avcodec_send_frame() 返回 AVERROR(EAGAIN) 的语义是
  //   “输入队列已满，本帧没有被接收”，正确做法是先把输出队列抽干再重试 send。
  //   当前代码把 EAGAIN 当成可忽略的“无所谓”，直接进入 receive 循环然后返回，
  //   于是这一帧**永久丢失**，且调用方无从得知。
  //   现在之所以不出问题，是因为每次 send 后都把 receive 抽干，队列永不积压。
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
/// 创建一个 AVFrame，根据 AVCodecContext，需要
/// 调用者释放
///////////////////////////////////////////////
AVFrame *XEncode::CreateFrame() {
  std::unique_lock<std::mutex> lock(mtx_);
  if (!c_)
    return nullptr;
  auto frame = av_frame_alloc();
  if (!frame) {
    std::cerr << "av_frame_alloc failed!" << std::endl;
    return nullptr;
  }
  frame->width = c_->width;
  frame->height = c_->height;
  frame->format = c_->pix_fmt;

  auto re = av_frame_get_buffer(frame, 0);
  if (re != 0) {
    std::cerr << "av_frame_get_buffer failed!";
    av_frame_free(&frame);
    PrintErr(re);
    return nullptr;
  }
  return frame;
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
  // [KNOWN-ISSUE-2] End() 不幂等、且不检查返回值：
  //   1) 重复调用会重复 send NULL；
  //   2) avcodec_send_frame() 的返回值完全没看；
  //   3) 没有 "已冲刷" 状态机，Encode() 在 flush 后再调也是 UB。
  //   这里 avcodec_receive_packet() 会返回 AVERROR_EOF，靠着“静默 break”才没崩。
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
