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
const char *out_filename = "resetokai.mp4";

int main(int argc, char *argv[]) {

  int ret = 0;

  /////////////////////////////////////////////////////////////////
  /// 解封装部分
  /////////////////////////////////////////////////////////////////

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

  /////////////////////////////////////////////////////////////////
  /// 重封装部分
  /////////////////////////////////////////////////////////////////

  // 输出编码器上下文
  AVFormatContext *out_fmt_ctx = nullptr;
  ret = avformat_alloc_output_context2(&out_fmt_ctx, NULL, NULL, out_filename);
  CHKERR(ret);

  // 添加视频流,音频流
  auto mvs = avformat_new_stream(out_fmt_ctx, NULL); // 视频流
  auto mas = avformat_new_stream(out_fmt_ctx, NULL); // 音频流

  // 打开输入IO
  ret = avio_open(&out_fmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
  CHKERR(ret);

  // 设置编码音视频流参数
  if (vs) {
    mvs->time_base = vs->time_base; // 时间基与原视频一致
    // 从解封装复制参数
    avcodec_parameters_copy(mvs->codecpar, vs->codecpar);
    mvs->codecpar->codec_tag =
        0; // 清理原封装器的 codec_tag, 让 MP4 封装器自动选择兼容的标签
  }
  if (as) {
    mas->time_base = as->time_base; // 时间基与原视频一致
    // 从解封装复制参数
    avcodec_parameters_copy(mas->codecpar, as->codecpar);
    mas->codecpar->codec_tag = 0;
  }

  // 写入文件头
  ret = avformat_write_header(out_fmt_ctx, NULL);
  CHKERR(ret);

  // 打印输出上下文
  av_dump_format(out_fmt_ctx, 0, out_filename, 1); // 1 表示输出上下文

  /////////////////////////////////////////////////////////////////
  /// 截取 10 ~ 20秒之间的音视频
  /// 取多不取少
  /// 假定 9 11 秒有关键帧 取 9秒（约定俗成）
  /////////////////////////////////////////////////////////////////

  AVPacket *pkt = av_packet_alloc();

  for (;;) {
    ret = av_read_frame(fmt_ctx, pkt);
    if (ret == AVERROR_EOF)
      break; // 正常结束，跳出循环执行 av_write_trailer
    CHKERR(ret);

    AVStream *in_stream = fmt_ctx->streams[pkt->stream_index];
    AVStream *out_stream = nullptr;
    if (vs && pkt->stream_index == vs->index) {
      std::cout << "视频流: ";
      // out_stream = out_fmt_ctx->streams[0];
      out_stream = mvs;
    } else if (as && pkt->stream_index == as->index) {
      std::cout << "音频流: ";
      // out_stream = out_fmt_ctx->streams[1];
      out_stream = mas;
    } else {
      // 丢弃字幕、tmcd 事件码等其他无关流，防止越界崩溃
      av_packet_unref(pkt);
      continue;
    }

    // 1. 更新包对应的输出流下标
    pkt->stream_index = out_stream->index;

    // 2. 将 pts / dts / duration 从输入 time_base 换算为 输出 time_base
    av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);
    pkt->pos = -1;

    // 3. 交错写入（成功时内部会自动 unref pkt）
    ret = av_interleaved_write_frame(out_fmt_ctx, pkt);
    if (ret != 0) {
      av_packet_unref(pkt);
      CHKERR(ret);
    }

    // 打印 pkt 信息
    std::cout << pkt->pts << " : " << pkt->dts << " : " << pkt->size
              << std::endl;

    // 释放 pkt防止内存泄露
    // av_packet_unref(pkt);
    // 睡眠 100ms
    // std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // 写入 尾部 trailer
  // MP4 的 moov 索引头是在写入文件尾时才正式生成并刷入磁盘的
  ret = av_write_trailer(out_fmt_ctx);
  CHKERR(ret);

  // 清理 AVIOContext
  avio_closep(&out_fmt_ctx->pb);
  // 清理输出上下文
  avformat_free_context(out_fmt_ctx);
  out_fmt_ctx = nullptr;

  // 释放 pkt
  av_packet_free(&pkt);

  // 清理输入上下文
  avformat_close_input(&fmt_ctx);
  return EXIT_SUCCESS;
}
