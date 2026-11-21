#include "xencode.h"
#include <fstream>
#include <iostream>
// 引用 ffmpeg 头文件
extern "C" {
#include <libavcodec/avcodec.h> // AVCodec AVCodecContext
#include <libavutil/avutil.h>   // AVFrame AVPacket
#include <libavutil/opt.h>      // av_opt_set
}

// ==========================================================
// 🌟 测试分辨率设置
// ==========================================================
const int YUV_WIDTH = 1920;
const int YUV_HEIGHT = 1080;
const int YUV_FRAME_RATE = 25;

// ==========================================================
// 🌟 测试开关：取消注释你想测试的模式（一次只能开一个）
// ==========================================================
// #define TEST_ABR // 平均码率 (避免使用，画质最差)
// #define TEST_CQP // 恒定 QP (画质好，体积不可控)
// #define TEST_CRF // 恒定速率因子 (点播首选，纯 VBR)
// #define TEST_CBR // 恒定比特率 (直播首选，带填充)
#define TEST_CRF_VBV // CRF + 约束 (高质量在线点播)

int main(int argc, char *argv[]) {
  std::string filename = std::to_string(YUV_WIDTH) + "_" +
                         std::to_string(YUV_HEIGHT) + "_" +
                         std::to_string(YUV_FRAME_RATE) + "_";

  AVCodecID codec_id = AV_CODEC_ID_H264;

  if (argc > 1) {
    std::string codec{argv[1]};
    if (codec == "h265" || codec == "hevc") {
      codec_id = AV_CODEC_ID_HEVC;
    }
  }

  // 根据开启的宏，给文件名加个后缀，方便对比大小
#if defined(TEST_ABR)
  filename += "abr";
#elif defined(TEST_CQP)
  filename += "cqp";
#elif defined(TEST_CRF)
  filename += "crf";
#elif defined(TEST_CBR)
  filename += "cbr";
#elif defined(TEST_CRF_VBV)
  filename += "crf_vbv";
#endif
  if (codec_id == AV_CODEC_ID_H264)
    filename += ".264";
  else if (codec_id == AV_CODEC_ID_HEVC)
    filename += ".265";

  std::ofstream ofs(filename, std::ios::binary);

  // 创建上下文
  XEncode en;
  auto c = XEncode::Create(codec_id);
  en.set_c(c);

  c->width = YUV_WIDTH;
  c->height = YUV_HEIGHT;

  int target_bitrate = 800000; // 测试用码率 800k

  // ==========================================================
  // 🌟 核心：根据宏定义配置码率控制算法
  // ==========================================================
#if defined(TEST_ABR)
  // 模式 1：ABR 平均比特率 (硬凑目标码率，画质波动大)
  c->bit_rate = target_bitrate;
  c->rc_max_rate = target_bitrate;
  c->rc_buffer_size = target_bitrate * 2;
  std::cout << "当前模式: ABR (平均码率)" << std::endl;
#elif defined(TEST_CQP)
  // 模式 2：CQP 恒定 QP （每一帧的压缩力度恒定，不管体积）
  c->flags |= AV_CODEC_FLAG_QSCALE;
  // c->global_quality = FF_QP2LAMBDA * 23; // 23 是 x264
  // 默认值，越小画质越好体积越大
  // av_opt_set_int(c->priv_data, "qp", 23, 0);
  en.SetOpt("qp", 23);
  std::cout << "当前模式: CQP (恒定 QP 23)" << std::endl;
#elif defined(TEST_CRF)
  // 模式 3：CRF 恒定质量（最常用，根据画质决定码率）
  // av_opt_set(c->priv_data, "crf", "18", 0);
  en.SetOpt("crf", 18);
  std::cout << "当前模式: CRF (恒定质量 CRF 23)" << std::endl;
#elif defined(TEST_CBR)
  // 模式 4：CBR 恒定比特率 (加填充，适合推流。注意：封装成 MP4
 会丢弃填充数据) 
  // av_opt_set(c->priv_data, "nal-hrd", "cbr", 0);
    en.SetOpt("nal-hrd", "cbr");
 c->bit_rate = target_bitrate;
 c->rc_min_rate = target_bitrate;
 c->rc_max_rate = target_bitrate;
 c->rc_buffer_size = target_bitrate * 2;
 std::cout << "当前模式: CBR (恒定码率 400k)" << std::endl;
#elif defined(TEST_CRF_VBV)
  // 模式 5：CRF + VBV 约束 (B站/YouTube 首选，保画质且防卡顿)
  en.SetOpt("crf", 18); // 恒定速率因子
  c->rc_max_rate = target_bitrate;
  c->rc_buffer_size = target_bitrate * 2; // 缓冲区
  std::cout << "当前模式: CRF + VBV 约束" << std::endl;
#endif

  en.Open(); // 打开编码器

  auto frame = en.CreateFrame();
  int count = 0; // 写入文件的帧数 SPS PPS IDR 放在一帧中

  std::cout << "开始编码..." << std::endl;
  for (int i = 0; i < 1000; ++i) {
    // 确保帧可写
    av_frame_make_writable(frame);

    // 生成AVFrame数据
    // 每帧数据不同
    // Y
    for (int y = 0; y < c->height; ++y) {
      for (int x = 0; x < c->width; ++x) {
        frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
      }
    }

    // UV
    for (int y = 0; y < c->height / 2; ++y) {
      for (int x = 0; x < c->width / 2; ++x) {
        frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
        frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 2;
      }
    }

    frame->pts = i;

    // 发送未压缩的帧到线程中压缩
    auto pkts = en.Encode(frame);
    for (auto pkt : pkts) {
      count++;
      ofs.write((char *)pkt->data, pkt->size);
      av_packet_free(&pkt); // 用完释放
    }
  }

  auto pkts = en.End(); // 将缓冲区内的frame冲刷出来
  for (auto pkt : pkts) {
    count++;
    ofs.write((char *)pkt->data, pkt->size);
    av_packet_free(&pkt);
  }

  // 释放输出文件流 ofs
  ofs.close();

  // 手动释放 AVFrame
  av_frame_free(&frame);

  std::cout << "Total Packet Encoded: " << count << std::endl;

  return EXIT_SUCCESS;
}
