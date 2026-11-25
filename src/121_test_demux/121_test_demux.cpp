#include <chrono>
#include <iostream>
#include <thread>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

void PrintErr(int re) {
  char buf[1024] = {0};
  av_strerror(re, buf, sizeof(buf) - 1);
  std::cout << buf << std::endl;
}

#define CHKERR(re)                                                             \
  if (re != 0) {                                                               \
    PrintErr(re);                                                              \
    getchar();                                                                 \
    return -1;                                                                 \
  }

const char *filename = "setokai.mp4";

int main(int argc, char *argv[]) {

  int ret = 0;
  // 输入全局上下文
  AVFormatContext *fmt_ctx = nullptr;
  // 打开媒体文件，解封装输出上下文
  ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL);
  CHKERR(ret);

  ret = avformat_find_stream_info(fmt_ctx, NULL);
  CHKERR(ret);

  // 打印封装函数信息
  av_dump_format(fmt_ctx, 0, filename,
                 0); // 0 表示上下文输入 1 表示上下文输出
  AVStream *vs = nullptr;
  AVStream *as = nullptr;
  // 拿到视频流和音频流
  for (int i = 0; i < fmt_ctx->nb_streams; ++i) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      vs = fmt_ctx->streams[i];
      std::cout
          << "========================= 视频 ============================="
          << std::endl;
      std::cout << fmt_ctx->streams[i]->codecpar->width << std::endl;
      std::cout << fmt_ctx->streams[i]->codecpar->height << std::endl;
    } else if (fmt_ctx->streams[i]->codecpar->codec_type ==
               AVMEDIA_TYPE_AUDIO) {
      as = fmt_ctx->streams[i];
      std::cout
          << "========================= 音频 ============================="
          << std::endl;
      std::cout << fmt_ctx->streams[i]->codecpar->sample_rate << std::endl;
    }
  }

  AVPacket *pkt = av_packet_alloc();

  for (;;) {
    ret = av_read_frame(fmt_ctx, pkt);
    CHKERR(ret);
    if (pkt->stream_index == vs->index)
      std::cout << "视频流: ";
    if (pkt->stream_index == as->index)
      std::cout << "音频流: ";

    // 打印 pkt 信息
    std::cout << pkt->pts << " : " << pkt->dts << " : " << pkt->size
              << std::endl;

    // 释放 pkt防止内存泄露
    av_packet_unref(pkt);
    // 睡眠 100ms
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // 清理输入上下文
  avformat_close_input(&fmt_ctx);

  // 释放 pkt
  av_packet_free(&pkt);
  return EXIT_SUCCESS;
}
