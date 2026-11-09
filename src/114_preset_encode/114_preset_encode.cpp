#include <fstream>
#include <iostream>
// 引用 ffmpeg 头文件
extern "C" {
#include <libavcodec/avcodec.h> // AVCodec AVCodecContext
#include <libavutil/avutil.h>   // AVFrame AVPacket
#include <libavutil/opt.h>      // av_opt_set
}

// ==========================================================
// 🌟 测试开关：取消注释你想测试的模式（一次只能开一个）
// ==========================================================
// #define TEST_ABR // 平均码率 (避免使用，画质最差)
// #define TEST_CQP // 恒定 QP (画质好，体积不可控)
// #define TEST_CRF // 恒定速率因子 (点播首选，纯 VBR)
// #define TEST_CBR // 恒定比特率 (直播首选，带填充)
#define TEST_CRF_VBV // CRF + 约束 (高质量在线点播)

int main(int argc, char *argv[]) {
  int re{};
  int opt_re{};
  std::string filename = "400_300_25_";

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

  // 1. 找到编码器
  auto codec = avcodec_find_encoder(codec_id);
  if (!codec) {
    std::cerr << "failed to find codec!" << std::endl;
    return -1;
  }

  // 2. 编码上下文的创建
  AVCodecContext *c = avcodec_alloc_context3(codec);
  if (!c)
    return -1;

  // 3. 设定基础参数
  c->width = 400;
  c->height = 300;
  c->pix_fmt = AV_PIX_FMT_YUV420P;
  c->time_base = {1, 25};
  c->thread_count = 16;

  // 预设参数（为测试效果明显，关闭 B 帧，开启 ultrafast）
  c->max_b_frames = 0;
  opt_re = av_opt_set(c->priv_data, "preset", "ultrafast", 0);
  if (!opt_re)
    std::cerr << "preset error!" << std::endl;

  opt_re = av_opt_set(c->priv_data, "tune", "zerolatency", 0);
  if (!opt_re)
    std::cerr << "preset error!" << std::endl;

  int target_bitrate = 400000; // 测试用 目标码率 400k

  // ==========================================================
  // 🌟 4. 核心：根据宏定义配置码率控制算法
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
  av_opt_set_int(c->priv_data, "qp", 23, 0);
  std::cout << "当前模式: CQP (恒定 QP 23)" << std::endl;
#elif defined(TEST_CRF)
  // 模式 3：CRF 恒定质量（最常用，根据画质决定码率）
  av_opt_set(c->priv_data, "crf", "23", 0);
  std::cout << "当前模式: CRF (恒定质量 CRF 23)" << std::endl;
#elif defined(TEST_CBR)
  // 模式 4：CBR 恒定比特率 (加填充，适合推流。注意：封装成 MP4 会丢弃填充数据)
  av_opt_set(c->priv_data, "nal-hrd", "cbr", 0);
  c->bit_rate = target_bitrate;
  c->rc_min_rate = target_bitrate;
  c->rc_max_rate = target_bitrate;
  c->rc_buffer_size = target_bitrate * 2;
  std::cout << "当前模式: CBR (恒定码率 400k)" << std::endl;
#elif defined(TEST_CRF_VBV)
  // 模式 5：CRF + VBV 约束 (B站/YouTube 首选，保画质且防卡顿)
  av_opt_set(c->priv_data, "crf", "23", 0);
  c->rc_max_rate = target_bitrate;        // 限制瞬间最大码率
  c->rc_buffer_size = target_bitrate * 2; // 缓冲区
  std::cout << "当前模式: CRF + VBV 约束" << std::endl;
#endif
  // ==========================================================

  // 5. 打开编码器
  re = avcodec_open2(c, codec, NULL);
  if (re != 0) {
    std::cerr << "Cannot open codec!" << std::endl;
    return -1;
  }

  // 6. 准备 Frame 和 Packet
  auto frame = av_frame_alloc();
  frame->width = c->width;
  frame->height = c->height;
  frame->format = c->pix_fmt;
  re = av_frame_get_buffer(frame, 0);
  if (re != 0) {
    char buf[1024] = {0};
    av_strerror(re, buf, sizeof(buf) - 1);
    std::cerr << "av_frame_get_buffer failed：" << buf << std::endl;
    return -1;
  }

  auto pkt = av_packet_alloc();

  std::cout << "开始编码..." << std::endl;

  // 7. 循环发送原始帧
  for (int i = 0; i < 250; ++i) {
    // 【关键修复】确保 frame 是可写的，防止内存踩踏
    av_frame_make_writable(frame);

    // 生成渐变画面
    for (int y = 0; y < c->height; ++y) {
      for (int x = 0; x < c->width; ++x) {
        frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
      }
    }
    for (int y = 0; y < c->height / 2; ++y) {
      for (int x = 0; x < c->width / 2; ++x) {
        frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
        frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
      }
    }

    frame->pts = i; // 设置帧的时间戳

    // 发送未压缩帧到线程中压缩
    re = avcodec_send_frame(c, frame);
    if (re < 0)
      break; // 偶尔一帧会发送失败，可能是编码器内部缓冲区满了

    while (re >= 0) { // 返回多帧
      re = avcodec_receive_packet(c, pkt);
      if (re == AVERROR(EAGAIN) || re == AVERROR_EOF) {
        break;
      }
      if (re < 0) { // 编码出错
        char buf[1024] = {0};
        av_strerror(re, buf, sizeof(buf) - 1);
        std::cerr << "avcodec_receive_packet failed!" << buf << std::endl;
        break;
      }
      std::cout << "current pkt size: " << pkt->size << "\n" << std::flush;
      ofs.write((char *)pkt->data, pkt->size);
      av_packet_unref(pkt);
    }
  }

  // 在完全保存前冲刷编码器（把缓存的帧全吐出来）
  avcodec_send_frame(c, NULL);
  while (true) {
    re = avcodec_receive_packet(c, pkt);
    if (re == AVERROR(EAGAIN) || re == AVERROR_EOF)
      break;
    if (re < 0)
      break;

    ofs.write((char *)pkt->data, pkt->size);
    av_packet_unref(pkt);
  }

  std::cout << "\n编码完成，文件已保存至: " << filename << std::endl;

  // 释放 ofs
  ofs.close();

  // 释放 packet
  av_packet_free(&pkt);

  // 释放 Frame
  av_frame_free(&frame);
  // 释放CodecContext
  avcodec_free_context(&c);
  return EXIT_SUCCESS;
}
