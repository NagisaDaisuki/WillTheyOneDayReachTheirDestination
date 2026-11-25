#include "xdecode.h"
#include "xvideoview.h"
#include <chrono>
#include <iostream>
#include <thread>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
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

  // 拿到视频编码器 ID
  auto video_codec_id = vs->codecpar->codec_id;
  // 视频编码信息
  auto video_par = vs->codecpar;

  /////////////////////////////////////////////////////////////////////
  /// 视频解码初始化
  XDecode de;
  AVCodecContext *dec_ctx = XDecode::Create(video_codec_id, false);
  // 解封装的视频编码参数，传递给解码上下文
  avcodec_parameters_to_context(dec_ctx, video_par);

  // 设置硬解
  auto type = AV_HWDEVICE_TYPE_VAAPI; // VAAPI 解码
  de.initHW(type);

  // 设置到解码器中，线程安全，设置完后 dec_ctx 不在其类外部使用
  de.set_c(dec_ctx);

  // 打开解码器
  if (!de.Open()) {
    std::cout << "decode Open failed!" << std::endl;
    return -1;
  }

  // 创建解码输出的空间
  auto frame = de.CreateFrame();
  /////////////////////////////////////////////////////////////////////

  /////////////////////////////////////////////////////////////////////
  /// 渲染初始化
  /// 在对 h264 裸流解码播放时需要读取到第一帧后才能拿到视频宽高数据
  /// 现在解封装直接得到视频参数
  auto view = XVideoView::Create();
  // view->Init(frame->width, frame->height,
  // (XVideoView::PixFormat)frame->format);
  view->Init(video_par->width, video_par->height,
             (XVideoView::PixFormat)video_par->format);
  /////////////////////////////////////////////////////////////////////

  AVPacket *pkt = av_packet_alloc();

  for (;;) {
    ret = av_read_frame(fmt_ctx, pkt);
    CHKERR(ret);
    if (pkt->stream_index == vs->index) {
      std::cout << "视频流: ";
      if (de.Send(pkt)) {
        while (de.Recv(frame)) {
          std::cout << frame->pts << " " << std::flush;
          view->DrawFrame(frame); // 渲染视频
        }
      }
    }
    if (pkt->stream_index == as->index) {
      std::cout << "音频流: ";
    }

    // 打印 pkt 信息
    // std::cout << pkt->pts << " : " << pkt->dts << " : " << pkt->size
    //           << std::endl;

    // 释放 pkt防止内存泄露
    av_packet_unref(pkt);
  }

  // 释放 frame
  av_frame_free(&frame);

  // 清理输入上下文
  avformat_close_input(&fmt_ctx);

  // 释放 pkt
  av_packet_free(&pkt);
  return EXIT_SUCCESS;
}
