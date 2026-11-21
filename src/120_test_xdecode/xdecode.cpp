#include "xdecode.h"
#include <iostream>
#include <thread>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

//////////////////////////////////////////////////
/// 发送解码
//////////////////////////////////////////////////
bool XDecode::Send(const AVPacket *pkt) {
  std::unique_lock<std::mutex> lock(mtx_);
  if (!c_)
    return false;

  int ret = avcodec_send_packet(c_, pkt);
  if (ret != 0)
    return false; // 不打印错误避免流媒体阻塞
  return true;
}

//////////////////////////////////////////////////
/// 获取解码
//////////////////////////////////////////////////
bool XDecode::Recv(AVFrame *frame) {
  std::unique_lock<std::mutex> lock(mtx_);
  if (!c_)
    return false;

  // 软解：直接解码到用户传进来的 frame 中
  if (!c_->hw_device_ctx) {
    auto re = avcodec_receive_frame(c_, frame);
    if (re == 0)
      return true;
    return false; // 失败直接返回，不能动原 frame
  }

  // 硬解：分配一个临时的 frame 接收显存数据
  AVFrame *tmp_hw_frame = av_frame_alloc();
  auto re = avcodec_receive_frame(c_, tmp_hw_frame);
  if (re == 0) {
    // 显存转内存，转移到用户的 frame 里
    re = av_hwframe_transfer_data(frame, tmp_hw_frame, 0);
    av_frame_free(&tmp_hw_frame); // 临时硬件帧用完必须立即释放
    if (re != 0) {
      PrintErr(re);
      return false;
    }
    return true;
  }

  // 硬解失败(如EAGAIN的清理)
  av_frame_free(&tmp_hw_frame);
  return false;
}

//////////////////////////////////////////////////
/// 获取缓存
//////////////////////////////////////////////////
std::vector<AVFrame *> XDecode::End() {
  std::vector<AVFrame *> res{};
  // 取出缓存当中的数据
  std::unique_lock<std::mutex> lock(mtx_);
  if (!c_)
    return res;

  auto re = avcodec_send_packet(c_, NULL); // 洗出缓冲区中的 pkt
  if (re < 0 && re != AVERROR_EOF)
    return res;

  while (true) {
    AVFrame *out_frame = nullptr;

    if (!c_->hw_device_ctx) { // 软解
      out_frame = av_frame_alloc();
      re = avcodec_receive_frame(c_, out_frame);
      if (re < 0) {
        av_frame_free(&out_frame);
        break;
      }
    } else { // 硬解
      AVFrame *hw_frame = av_frame_alloc();
      re = avcodec_receive_frame(c_, hw_frame);
      if (re < 0) {
        av_frame_free(&hw_frame); // 失败，释放硬件帧
        break;
      }
      out_frame = av_frame_alloc();
      re = av_hwframe_transfer_data(out_frame, hw_frame, 0);
      av_frame_free(&hw_frame); // 硬件帧用完立即销毁，不管成功失败
      if (re != 0) {
        PrintErr(re);
        av_frame_free(&out_frame); // 转换失败，释放软件帧
        break;
      }
    }
    res.push_back(out_frame);
    std::cout << out_frame->format << "-" << std::flush;
  }

  return res;
}

//////////////////////////////////////////////////
/// 初始化硬件加速 初始值3-> AV_HWDEVICE_TYPE_VAAPI
//////////////////////////////////////////////////
bool XDecode::initHW(int type) {
  std::unique_lock<std::mutex> lock(mtx_);
  if (!c_)
    return false;

  AVBufferRef *hw_device_ctx = nullptr; // 硬件加速上下文
  auto re = av_hwdevice_ctx_create(&hw_device_ctx, (AVHWDeviceType)type, NULL,
                                   NULL, 0);
  if (re != 0) {
    PrintErr(re);
    return false;
  }
  c_->hw_device_ctx = hw_device_ctx;
  std::cout << "Hardware Acceleration: " << type << std::endl;
  return true;
}
