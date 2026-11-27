#include "xdecode.h"
#include "xdemux.h"
#include "xencode.h"
#include "xmux.h"
#include "xsws.h"
#include <cstdlib>
#include <iostream>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
}

//////////////////////////////////////////////////////////////
/// 设置截断时间
//////////////////////////////////////////////////////////////
int begin_sec{0};
int end_sec{0};
long long video_begin_pts{};
long long audio_begin_pts{};
long long video_end_pts{};
long long audio_end_pts{};

//////////////////////////////////////////////////////////////
/// 设置转码宽高
//////////////////////////////////////////////////////////////
int video_width{};
int video_height{};

//////////////////////////////////////////////////////////////
/// 设置调试参数
//////////////////////////////////////////////////////////////
int video_frame_count{};
int audio_frame_count{};
double total_sec{}; // 必须是浮点，int 会把每帧 1/24 秒截断成 0

void print_usage() {
  std::string usage = "Usage: 125_record_remux input_file output_file "
                      "start_sec end_sec video_width(px) video_height(px)\n";
  std::string example = "Example: 125_record_remux setokai.mp4 "
                        "setokai_truncate.mp4 10 30 1280 720\n";
  std::cout << usage << example << std::endl;
}

int main(int argc, char *argv[]) {
  int re{};
  print_usage();
  if (argc < 3)
    return -1;
  std::string in_file{argv[1]};
  std::string out_file{argv[2]};

  if (argc > 3)
    begin_sec = atoi(argv[3]);
  if (argc > 4)
    end_sec = atoi(argv[4]);

  if (argc > 6) {
    video_width = atoi(argv[5]);
    video_height = atoi(argv[6]);
  }

  //////////////////////////////////////////////////////////////
  /// 解封装部分
  //////////////////////////////////////////////////////////////
  XDemux demux;
  AVFormatContext *demux_c = XDemux::Open(in_file.c_str());
  if (!demux_c) {
    std::cerr << "打开输入文件失败：" << in_file << std::endl;
    return -1;
  }
  demux.set_c(demux_c);

  // 校验输入有没有视频流。纯音频文件时
  // demux.video_index() 返回 -1，后面的 streams[-1] 会直接越界崩溃。
  if (demux.video_index() < 0) {
    std::cerr << "输入文件不含视频流：" << in_file << std::endl;
    return -1;
  }

  //////////////////////////////////////////////////////////////
  /// 视频解码初始化
  //////////////////////////////////////////////////////////////
  XDecode de;
  auto dec_ctx = XDecode::Create(demux.video_codec_id(), false);
  // 设定视频解码器参数
  demux.CopyPara(demux.video_index(), dec_ctx);
  de.set_c(dec_ctx); // 在函数内部释放 dec_ctx, 之后不在非当前类内操作 dec_ctx
  // 打开解码器
  de.Open();

  // 解码后存储
  auto frame = de.CreateFrame();

  //////////////////////////////////////////////////////////////
  /// 视频编码初始化
  //////////////////////////////////////////////////////////////
  if (demux.video_index() >= 0) {
    if (video_width <= 0)
      video_width = demux_c->streams[demux.video_index()]->codecpar->width;
    if (video_height <= 0)
      video_height = demux_c->streams[demux.video_index()]->codecpar->height;
  }
  XEncode en;
  auto enc_ctx = XEncode::Create(AV_CODEC_ID_HEVC, true); // 统一转成 H265
  enc_ctx->framerate = demux_c->streams[demux.video_index()]
                           ->avg_frame_rate; // 根据解封装视频流帧率设置
  // avg_frame_rate 可能是 0/0（封装里没写
  // r_frame_rate/avg_frame_rate，或只有 1 个时间戳算不出来），
  // av_inv_q(0/0) 得到 1/0 这种非法时间基，编码器 open 后直接算出
  // 除零/无穷 pts。
  if (enc_ctx->framerate.num <= 0 || enc_ctx->framerate.den <= 0)
    enc_ctx->framerate = {25, 1};
  enc_ctx->time_base = av_inv_q(enc_ctx->framerate);
  // 说明：改前写死 {1,25}（见 XCodec::Create），跟源的 24fps 不匹配，
  // 编码器会把 frame->pts 当成 1/25 秒的帧号，时间轴整体错位。
  // enc_ctx->time_base = demux_c->streams[demux.video_index()]->time_base;
  enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  enc_ctx->width = video_width;
  enc_ctx->height = video_height;
  en.set_c(enc_ctx);
  en.Open();

  //////////////////////////////////////////////////////////////
  /// 封装部分
  //////////////////////////////////////////////////////////////
  XMux mux;
  // 改前问题：不管输入有没有音频/视频都建两条流，纯视频源写头直接失败。
  // 按输入实际存在的流来建输出流。
  AVFormatContext *mux_c = XMux::Open(
      out_file.c_str(), demux.video_index() >= 0, demux.audio_index() >= 0);
  if (!mux_c) {
    std::cerr << "打开输出文件失败：" << out_file << std::endl;
    return -1;
  }
  mux.set_c(mux_c);

  // 设置编码音视频流参数并建立 输入流索引 -> 输出流索引 映射
  // 输入和输出的 stream_index 不保证一致，不映射就会写错流
  if (demux.video_index() >= 0 && mux.video_index() >= 0) {
    auto mvs = mux_c->streams[mux.video_index()];
    mvs->time_base = enc_ctx->time_base;
    // mvs->time_base = {demux.video_time_base().num,
    // demux.video_time_base().den};
    //  复制视频数据
    avcodec_parameters_from_context(mux_c->streams[mux.video_index()]->codecpar,
                                    enc_ctx);
    // demux.CopyPara(demux.video_index(), mvs->codecpar);
    mux.SetStreamMap(demux.video_index(), mux.video_index());
  }
  if (demux.audio_index() >= 0 && mux.audio_index() >= 0) {
    auto mas = mux_c->streams[mux.audio_index()];
    mas->time_base = {demux.audio_time_base().num, demux.audio_time_base().den};
    // 复制音频数据
    demux.CopyPara(demux.audio_index(), mas->codecpar);
    mux.SetStreamMap(demux.audio_index(), mux.audio_index());
  }

  // 写入文件头，失败必须终止：header 写失败后 muxer 内部状态未初始化，
  // 继续 write/写 trailer 会直接段错误
  if (!mux.WriteHeader())
    return -1;

  // 改前问题：写包时用的是“自己设的”流时间基，但 avformat_write_header
  // 可能改写 st->time_base（MP4 会把 timescale 抬到 >=10000，例如
  // 1/24 → 1/12288）。此时再按 1/24 写包，时间戳会被当成 1/12288
  // 单位，10 秒的片子变成 0.018 秒。必须用写头【之后】的 tb。
  const AVRational video_out_tb = mux_c->streams[mux.video_index()]->time_base;

  // 根据开始/结束秒数算出输入流各自的 pts
  // 注意：end_sec 的换算不能依赖 begin_sec，否则 "0 30" 这种截断会静默失效
  if (demux.video_index() >= 0 && demux.video_time_base().num > 0) {
    double t = (double)demux.video_time_base().den /
               (double)demux.video_time_base().num;
    video_begin_pts = begin_sec * t;
    if (end_sec > 0)
      video_end_pts = end_sec * t;
    if (begin_sec > 0)
      demux.Seek(video_begin_pts, demux.video_index()); // 移动到开始帧
  }
  if (demux.audio_index() >= 0 && demux.audio_time_base().num > 0) {
    double t = (double)demux.audio_time_base().den /
               (double)demux.audio_time_base().num;
    audio_begin_pts = begin_sec * t;
    if (end_sec > 0)
      audio_end_pts = end_sec * t;
  }

  //////////////////////////////////////////////////////////////
  /// 实例化缩放器
  //////////////////////////////////////////////////////////////
  XSws sws;
  AVFrame *scaled_frame = av_frame_alloc(); // 专门用来装缩放后的画面

  AVPacket *pkt = av_packet_alloc();
  bool read_error{false};
  // 交给编码器的单调帧序号，单位 = enc_ctx->time_base
  // 改前问题：直接把解码帧的 pts 送到编码器。解码帧的 pts 属于输入流
  // 时间基（如 1/12288，每帧步进 512），编码器却按 1/framerate 解释，
  // 于是每帧被当成“过了 512/24 ≈ 21 秒”，dts/pts 彻底错乱。
  long long enc_frame_index{0};

  // 缩放 -> 编码 -> 写封装的统一入口，全程只用编码器时间基
  auto encode_frame = [&](const AVFrame *src) {
    // 改前问题：Scale 失败只打印一句，接着仍然拿上一帧残留的
    // scaled_frame 去编码，输出了重复画面。这里失败就直接丢帧。
    if (!sws.Scale(src, scaled_frame, enc_ctx->width, enc_ctx->height,
                   enc_ctx->pix_fmt)) {
      std::cerr << "Scale failed!" << std::endl;
      return;
    }
    scaled_frame->pts = enc_frame_index++;

    auto out_pkts = en.Encode(scaled_frame);
    for (auto out : out_pkts) {
      out->stream_index = mux.video_index();
      // 编码器时间基 -> 写头之后的封装时间基
      av_packet_rescale_ts(out, enc_ctx->time_base, video_out_tb);
      mux.Write(out);
      av_packet_free(&out);
    }
  };

  for (;;) {
    if (!demux.Read(pkt)) {
      // 只有非 EOF 的失败才是真错误
      read_error = !demux.eof();
      break;
    }

    const bool is_video = pkt->stream_index == demux.video_index();
    const bool is_audio = pkt->stream_index == demux.audio_index();

    // 视频超过结束时间：整体截断结束
    if (is_video && video_end_pts > 0 && pkt->pts > video_end_pts) {
      av_packet_unref(pkt);
      break;
    }
    // 音频超过结束时间：丢掉该帧继续，保证视频尾部不被提前截断
    if (is_audio && audio_end_pts > 0 && pkt->pts > audio_end_pts) {
      av_packet_unref(pkt);
      continue;
    }

    if (is_video) {
      // 改前问题（三重叠加）：
      // 1) 这里对【输入包】做了一次 RescaleTime，把 pts 减掉了
      //    video_begin_pts；后面编码后又减一次，同一个 offset 减了两遍，
      //    实测输出视频包 pts 从 -73728（约 -6 秒）开始；
      // 2) 输入包 pts 属于输入流时间基，解码器也按这个时间基输出
      //    frame->pts，提前改它会让后续 frame->pts 全错；
      // 3) begin 之前的包直接丢掉的话，会把 Seek 落点那个关键帧一起
      //    丢掉，后面整个 GOP 的 P/B 帧都参考不到，实测 121 个包
      //    只能解出 80 帧。
      // 正确做法：输入包 pts 一点不动，全部送进解码器，
      //           在【解码后】按 frame->pts 丢弃 begin 之前的画面。
      if (de.Send(pkt)) {
        while (de.Recv(frame)) {
          // Seek 会落到 begin 之前的关键帧，这段 GOP 预波只丢画面，
          // 包必须照常解码，否则关键帧的后续帧全部解不出来
          if (frame->pts != AV_NOPTS_VALUE && frame->pts < video_begin_pts)
            continue;
          encode_frame(frame);
        }
      }

      video_frame_count++;
      av_packet_unref(pkt);
    } else if (is_audio) {
      // 改前问题：pkt->stream_index 本来就是输入索引，这行赋值是空操作，
      // 却在后面被当成“已映射”用，容易误以为已经换成输出索引。
      // 改前问题：没有丢弃 begin 之前的包，只是把 pts 减去
      // audio_begin_pts，于是 Seek 预波全部变成负 pts 写进容器
      // （实测音频首个 pts = -44372）。
      if (pkt->pts != AV_NOPTS_VALUE && pkt->pts < audio_begin_pts) {
        av_packet_unref(pkt);
        continue;
      }
      // 偏移并转换时间基（RescaleTime 内部会把输入索引映射成输出索引）
      if (!mux.RescaleTime(pkt, audio_begin_pts, demux.audio_time_base())) {
        av_packet_unref(pkt);
        continue;
      }
      audio_frame_count++;
    } else {
      // 字幕/数据等其它流不处理
      av_packet_unref(pkt);
      continue;
    }

    // 写入音频帧（视频包已在视频分支里写完）
    // 改前问题：用 pkt->stream_index 和 mux.video_index() 比较来判断是不是
    // 视频，但 RescaleTime 已经把 pkt->stream_index 换成了输出索引，
    // 这种比法只有在“输入索引恰好等于输出索引”时才碰巧正确。换个流顺序
    // 不同的容器就会把音频包写进视频流（反之亦然）。这里直接用 is_audio。
    // 改前问题：for (auto pkt : epkt) 里的 pkt 遮蔽了外层 pkt，free 后
    // 也没置空，epkt 里留下一堆悬垂指针；且 break 只跳出内层 for，
    // 剩下的编码包直接泄死。
    if (is_audio) {
      if (!mux.Write(pkt))
        break;
    }
  }

  // 改前问题：从来没有冲刷解码器。解码器（尤其是开了多线程/有 B 帧时）
  // 内部会缓存 has_b_frames + 线程数 帧，不送 NULL 就永远取不出来，
  // 实测尾部直接丢了 17 帧（约 0.7 秒）。
  for (auto tail_frame : de.End()) {
    if (tail_frame->pts == AV_NOPTS_VALUE || tail_frame->pts >= video_begin_pts)
      encode_frame(tail_frame);
    av_frame_free(&tail_frame);
  }

  // 将编码器缓存中的视频帧刷出来
  // 改前问题：又调了一次 RescaleTime(pkt, video_begin_pts, 输入时间基)，
  // 等于把 begin 偏移减了第二次；而且编码器输出的包时间基是
  // enc_ctx->time_base，不是输入流时间基，拿输入时间基换算本身就是错的。
  auto end_pkts = en.End();
  for (auto out : end_pkts) {
    out->stream_index = mux.video_index();
    av_packet_rescale_ts(out, enc_ctx->time_base, video_out_tb);
    mux.Write(out);
    av_packet_free(&out);
  }

  // 写入 尾部trailer
  // MP4 的 moov 索引头是在写入文件尾时才正式生成并刷入磁盘
  mux.WriteEnd();

  // 释放 frame
  av_frame_free(&scaled_frame);
  av_frame_free(&frame);

  // 释放 pkt
  av_packet_free(&pkt);

  // 清理全局上下文
  mux.set_c(nullptr);
  demux.set_c(nullptr);
  std::cout << "\n输出文件：" << out_file << '\n';
  std::cout << "视频帧：" << video_frame_count << '\n';
  std::cout << "音频帧：" << audio_frame_count << '\n';
  // 改前问题：total_sec 累加的是 video 分支里 pkt->duration，但那个 pkt
  // 早就被 RescaleTime 改过 pts/stream_index，且 include 了 Seek 预波，
  // 统计值跟实际写入的时长基本对不上。直接用实际编码帧数算。
  total_sec = enc_frame_index / av_q2d(enc_ctx->framerate);
  std::cout << "总时长：" << total_sec << " 秒\n";
  if (read_error)
    std::cerr << "警告：读取源文件时发生错误，输出可能不完整" << std::endl;

  return EXIT_SUCCESS;
}
