#include "glview.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
}

/////////////////////////////////////////////////////////////////////////
//// 硬解必须提供 get_format 回调
//// 只设置 hw_device_ctx 而不给 get_format，FFmpeg 会继续用软件像素格式，
//// 解码器根本不会走 GPU，后面拿到的也不是 VAAPI surface
/////////////////////////////////////////////////////////////////////////
static enum AVPixelFormat GetHwFormat(AVCodecContext *ctx,
                                      const enum AVPixelFormat *fmts) {
  for (const enum AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
    if (*p == AV_PIX_FMT_VAAPI)
      return *p;
  }
  return avcodec_default_get_format(ctx, fmts);
}

int main(int argc, char *argv[]) {
  std::string filename = argc > 1 ? argv[1] : "setokai.h264";

  std::ifstream ifs(filename, std::ios::binary);
  if (!ifs) {
    std::fprintf(stderr, "failed to open file %s\n", filename.c_str());
    return -1;
  }

  /////////////////////////////////////////////////////////////////////////
  //// 1. 创建 VAAPI 硬解设备
  ////    /dev/dri/renderD128 是渲染节点（不带显示权限也能用），
  ////    传 NULL 时 FFmpeg 会自己找，但多卡/无 X 环境显式指定更稳
  /////////////////////////////////////////////////////////////////////////
  AVBufferRef *hw_device = nullptr;
  int ret = av_hwdevice_ctx_create(&hw_device, AV_HWDEVICE_TYPE_VAAPI,
                                   "/dev/dri/renderD128", nullptr, 0);
  if (ret < 0) {
    std::fprintf(stderr, "av_hwdevice_ctx_create(vaapi) failed\n");
    return -1;
  }

  // 按扩展名选解码器：.h264 走 H.264，.hevc/.h265 走 HEVC
  AVCodecID codec_id = AV_CODEC_ID_H264;
  {
    std::string lower = filename;
    for (char &c : lower)
      c = (char)std::tolower((unsigned char)c);
    if (lower.find("265") != std::string::npos ||
        lower.find("hevc") != std::string::npos)
      codec_id = AV_CODEC_ID_HEVC;
  }
  const AVCodec *codec = avcodec_find_decoder(codec_id);
  if (!codec) {
    std::fprintf(stderr, "avcodec_find_decoder failed\n");
    return -1;
  }

  // 先确认这份 FFmpeg 的解码器真的支持 VAAPI
  bool vaapi_supported = false;
  for (int i = 0;; ++i) {
    const AVCodecHWConfig *cfg = avcodec_get_hw_config(codec, i);
    if (!cfg)
      break;
    if (cfg->device_type == AV_HWDEVICE_TYPE_VAAPI &&
        (cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
      vaapi_supported = true;
      break;
    }
  }
  if (!vaapi_supported) {
    std::fprintf(stderr, "%s 不支持 VAAPI 硬解\n", codec->name);
    return -1;
  }

  AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
  if (!codec_ctx) {
    std::fprintf(stderr, "avcodec_alloc_context3 failed\n");
    return -1;
  }
  codec_ctx->hw_device_ctx = av_buffer_ref(hw_device);
  codec_ctx->get_format = GetHwFormat;
  // 硬解由 GPU 队列完成，thread_count 对解码线程无效，不用设

  ret = avcodec_open2(codec_ctx, codec, nullptr);
  if (ret != 0) {
    std::fprintf(stderr, "avcodec_open2 failed\n");
    return -1;
  }

  AVCodecParserContext *parser = av_parser_init(codec_id);
  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();

  GLVideoView view;
  bool inited = false;
  bool quit = false;
  int frames = 0;

  auto begin = std::chrono::steady_clock::now();
  unsigned char inbuf[4096] = {0};

  while (!ifs.eof() && !quit) {
    ifs.read((char *)inbuf, sizeof(inbuf));
    int data_size = (int)ifs.gcount();
    if (data_size <= 0)
      break;

    unsigned char *data = inbuf;
    while (data_size > 0 && !quit) {
      // 通过 00 00 01 起始码切出一个完整的 NAL 包
      int used = av_parser_parse2(parser, codec_ctx, &pkt->data, &pkt->size,
                                  data, data_size, AV_NOPTS_VALUE,
                                  AV_NOPTS_VALUE, 0);
      data += used;
      data_size -= used;

      if (pkt->size == 0)
        continue;

      if (avcodec_send_packet(codec_ctx, pkt) < 0)
        continue;

      while (true) {
        ret = avcodec_receive_frame(codec_ctx, frame);
        if (ret < 0)
          break;

        ///////////////////////////////////////////////////////////////////
        //// 2. 零拷贝：这里不再调用 av_hwframe_transfer_data
        ////    frame 的格式是 AV_PIX_FMT_VAAPI，数据一直在显存里，
        ////    直接交给 GLVideoView 用 dma_buf 导入 GL 纹理
        ///////////////////////////////////////////////////////////////////
        if (frame->format != AV_PIX_FMT_VAAPI) {
          std::fprintf(stderr,
                       "意外的像素格式 %s，说明没有真正走硬解\n",
                       av_get_pix_fmt_name((AVPixelFormat)frame->format));
          quit = true;
          break;
        }

        if (!inited) {
          if (!view.Init(frame->width, frame->height)) {
            std::fprintf(stderr, "GLVideoView::Init failed\n");
            return -1;
          }
          inited = true;
          std::printf("解码尺寸 %dx%d\n", frame->width, frame->height);
        }

        if (!view.DrawFrame(frame)) {
          std::fprintf(stderr, "DrawFrame failed\n");
          quit = true;
          break;
        }
        ++frames;

        if (view.IsExit()) {
          quit = true;
          break;
        }
      }
    }
  }

  // 冲刷解码器里剩余的帧
  if (!quit && avcodec_send_packet(codec_ctx, nullptr) >= 0) {
    while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
      if (frame->format != AV_PIX_FMT_VAAPI)
        continue;
      view.DrawFrame(frame);
      ++frames;
      if (view.IsExit())
        break;
    }
  }

  auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - begin);
  if (cost.count() > 0) {
    std::printf("\n总帧数 %d，耗时 %lld ms，平均 %.1f fps\n", frames,
                (long long)cost.count(), frames * 1000.0 / cost.count());
  }

  view.Close();

  av_frame_free(&frame);
  av_packet_free(&pkt);
  av_parser_close(parser);
  avcodec_free_context(&codec_ctx);
  av_buffer_unref(&hw_device);
  return 0;
}
