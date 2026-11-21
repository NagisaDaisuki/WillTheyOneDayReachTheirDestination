#include "xdecode.h"
#include "xsdl.h"
#include "xvideoview.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

const std::string filename = "setokai.h264";

int main(int argc, char *argv[]) {

  int re{};
  auto view = XVideoView::Create();

  // 1. 分割 h.264 存入 AVPacket
  std::ifstream ifs;
  ifs.open(filename, std::ios::binary);
  if (!ifs) {
    std::cerr << "failed to open file " << filename << std::endl;
    return -1;
  }

  unsigned char inbuf[4096] = {0};
  AVCodecID codec_id = AV_CODEC_ID_H264;

  // 找解码器
  XDecode decode;
  auto sw_ctx = XDecode::Create(codec_id, false);
  auto codec = sw_ctx->codec;
  // 设置解码器
  decode.set_c(sw_ctx);

  // const AVCodec *codec = avcodec_find_decoder(codec_id);
  // if (!codec) {
  //   std::cerr << "avcodec_find_decoder failed!" << std::endl;
  //   return -1;
  // }

  // 创建解码器上下文(软解)
  // AVCodecContext *sw_ctx = avcodec_alloc_context3(codec);
  // if (!sw_ctx) {
  //   std::cerr << "avcodec_alloc_context3 failed!" << std::endl;
  //   return -1;
  // }

  auto hw_type = AV_HWDEVICE_TYPE_VAAPI;

  // 打印所有支持的硬件加速方式
  for (int i = 0;; ++i) {
    auto config = avcodec_get_hw_config(codec, i);
    if (!config)
      break; // 未取到任何支持方式
    if (config->device_type)
      std::cout << av_hwdevice_get_type_name(config->device_type) << std::endl;
  }

  // 初始化硬件加速上下文
  decode.initHW(hw_type);

  // AVBufferRef *hw_ctx = nullptr;
  // av_hwdevice_ctx_create(&hw_ctx, hw_type, NULL, NULL, 0);

  // // 添加引用计数
  // sw_ctx->hw_device_ctx = av_buffer_ref(hw_ctx);

  // // 打开多线程
  // sw_ctx->thread_count = 16;

  // // 打开解码器
  decode.Open();
  // re = avcodec_open2(sw_ctx, NULL, NULL);
  // if (re != 0) {
  //   std::cerr << "avcodec_open2 failed!" << std::endl;
  //   return -1;
  // }

  // 手动释放 这些分配的内存资源
  // 初始化分割上下文
  auto parser = av_parser_init(codec_id);

  // pkt的创建
  auto pkt = av_packet_alloc();

  // frame的创建
  auto frame = av_frame_alloc();

  // 设置 Begin 时间 (时间点)
  auto time_point = NowTimePoint();
  int count = 0; // 解码统计
  bool is_init_win = false;

  // 循环播放：读到文件尾后回到开头；点窗口关闭按钮时 IsExit() 返回 true 退出
  while (!view->IsExit()) {
    ifs.read((char *)inbuf,
             sizeof(inbuf)); // 将h.264裸流文件按 4096大小复制到 inbuf内

    if (ifs.eof()) {
      ifs.clear(); // 清除结尾 EOF标志
      ifs.seekg(0, std::ios::beg);
    }

    int data_size = ifs.gcount(); // 读取的字节数
    if (data_size <= 0)
      break;

    auto data = inbuf;

    while (data_size > 0) {
      // 通过  0001 截断输出到 AVPacket，返回帧大小
      int ret = av_parser_parse2(parser, sw_ctx,
                                 &pkt->data, // 输出数据
                                 &pkt->size, // 输出数据大小
                                 data,       // 输入数据
                                 data_size,  // 输入数据大小
                                 AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

      // 移动指针位置
      data += ret;

      // 已处理的数据减去
      data_size -= ret;

      if (pkt->size) {
        // 发送 packet 到解码线程
        if (!decode.Send(pkt)) // 如果发送失败直接退出
          break;

        while (decode.Recv(frame)) {
          // 第一帧初始化窗口
          if (!is_init_win) {
            view->Init(frame->width, frame->height,
                       (XVideoView::PixFormat)frame->format);
            is_init_win = true;
          }
          view->DrawFrame(frame);
          count++;
          auto cur_time_point = NowTimePoint();
          if (cur_time_point - time_point >= std::chrono::milliseconds(100)) {
            std::cout << "\nfps = " << count * 10 << std::endl;
            count = 0;
            time_point = cur_time_point;
          } // 1/10 秒钟计算一次
        }
      }
    }
  }

  // 取出缓冲区当中的数据
  auto frames = decode.End();
  for (auto f : frames) {
    view->DrawFrame(f);
    av_frame_free(&f);
  }

  // 释放 pkt
  av_packet_free(&pkt);

  // 释放 frame
  av_frame_free(&frame);

  // 释放上下文
  // avcodec_free_context(&sw_ctx);

  // 关闭切割上下文
  av_parser_close(parser);
  return EXIT_SUCCESS;
}
