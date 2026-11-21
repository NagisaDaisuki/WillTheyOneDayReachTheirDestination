// ============================================================================
//  116_test_xencode —— 崩溃修复快照 (BUGFIX SNAPSHOT)
//
//  本目录保存的是「原崩溃 bug 已修复」的版本，代码逻辑与修复前的工程一致，
//  仅在修复处加了 [BUGFIX-n] 标记、在仍存在的隐患处加了 [KNOWN-ISSUE] 标记。
//  详细说明见同目录 README.md。
//
//  原始崩溃现象：SIGSEGV（Address boundary error / Segfault, core dumped）
//  崩溃位置   ：本文件 UV 填充循环（下方 [BUGFIX-1]）
//  根因       ：YUV420P 色度平面是亮度平面的 1/2 x 1/2，循环却用了整幅尺寸
// ============================================================================

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
// [BUGFIX-2] 分辨率常量化
//   修复前：编码尺寸在 c->width/height 里写死 1920x1080，文件名却仍是
//           "400_300_25_"，导致输出文件名与实际分辨率不一致，排查时极具误导性。
//   修复后：分辨率只在这一处定义，既用于配置编码器，也用于生成文件名；
//           帧平面的尺寸校验也依赖这两个常量。
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
  // [BUGFIX-3] Create() 是 static 成员函数，与对象状态无关。
  //   修复前写成 en.Create(codec_id)（用对象调用静态函数），语义上具有误导性，
  //   会让人以为结果依赖于 en 的状态。修复后统一为 XEncode::Create(...)。
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
    // [KNOWN-ISSUE-4] av_frame_make_writable() 的返回值未检查。
    //   失败时（例如帧缓冲区仍被编码器引用）应返回 AVERROR(EINVAL)，
    //   此处忽略会导致后续直接写只读/共享缓冲区。当前因为每次都整帧覆写
    //   且编码器已同步，实际不会触发，但契约上应该检查。
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
    // ======================= ★★★ 崩溃修复点 ★★★ =======================
    // [BUGFIX-1] 【主崩溃根因 / 唯一导致 SIGSEGV 的地方】
    //
    //   修复前：
    //     for (int y = 0; y < c->height; ++y)
    //       for (int x = 0; x < c->width;  ++x)   // ← 用了亮度平面尺寸
    //
    //   为什么错：YUV420P 的色度是 2x2 下采样，U/V 平面只有
    //     width/2 x height/2 个采样点。实测（gdb @ 1920x1080）：
    //       frame->linesize[0] = 1920   frame->linesize[1] = 960  frame->linesize[2] = 960
    //       frame->buf[0]->size = 3133568 （三平面共用 av_malloc 的一块内存）
    //       U 起始偏移 2088992，V 起始偏移 2611264 → V 平面可用 522304 字节
    //     而错误循环写到的最大偏移 = (1080-1)*960 + (1920-1) = 1037759
    //     → 越界 1037759 - 522304 ≈ 2 倍，直接写出 mmap 分配区 → SIGSEGV。
    //
    //   连带隐患：U 平面同样越界（越界约 515KB），但因为紧邻的 V 平面内存
    //   仍可写，所以不报错，只是**静默破坏 V 平面数据**——比崩溃更危险。
    //
    //   修复后：循环上界改为 c->height / 2 与 c->width / 2，与 YUV420P 一致。
    // ===================================================================
    for (int y = 0; y < c->height / 2; ++y) {
      for (int x = 0; x < c->width / 2; ++x) {
        frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
        frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 2;
      }
    }

    frame->pts = i;

    // 发送未压缩的帧到线程中压缩
    // [KNOWN-ISSUE-3] Encode() 内部在 avcodec_send_frame() 返回 EAGAIN 时不会重试，
    //   会静默丢掉这一帧（详见 xencode.cpp 中的 [KNOWN-ISSUE-3]）。
    //   当前实现每次 send 后立刻把输出队列抽干，所以实际不会触发 EAGAIN。
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

  // [BUGFIX-4] 统计语义修正
  //   修复前打印 "Total Frame Encoded"，但 count 统计的是 AVPacket 的个数，
  //   在存在 B 帧重排/一帧多包（如 SPS+PPS+IDR 分包）的情况下 packet 数 != frame 数。
  std::cout << "Total Packet Encoded: " << count << std::endl;

  return EXIT_SUCCESS;
}
