#include "xdemux.h"
#include "xmux.h"
#include <cstdlib>
#include <iostream>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
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
/// 设置调试参数
//////////////////////////////////////////////////////////////
int video_frame_count{};
int audio_frame_count{};
double total_sec{}; // 必须是浮点，int 会把每帧 1/24 秒截断成 0

void print_usage() {
  std::string usage =
      "Usage: 124_test_xformat input_file output_file start_sec end_sec\n";
  std::string example =
      "Example: 124_test_xformat setokai.mp4 setokai_truncate.mp4 10 30\n";
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

  //////////////////////////////////////////////////////////////
  /// 封装部分
  //////////////////////////////////////////////////////////////
  XMux mux;
  AVFormatContext *mux_c = XMux::Open(out_file.c_str());
  if (!mux_c) {
    std::cerr << "打开输出文件失败：" << out_file << std::endl;
    return -1;
  }
  mux.set_c(mux_c);

  // 设置编码音视频流参数并建立 输入流索引 -> 输出流索引 映射
  // 输入和输出的 stream_index 不保证一致，不映射就会写错流
  if (demux.video_index() >= 0 && mux.video_index() >= 0) {
    auto mvs = mux_c->streams[mux.video_index()];
    mvs->time_base = {demux.video_time_base().num, demux.video_time_base().den};
    // 复制视频数据
    demux.CopyPara(demux.video_index(), mvs->codecpar);
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

  AVPacket *pkt = av_packet_alloc();
  bool read_error{false};

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
      // RescaleTime 内部会把 stream_index 换算成输出流索引
      if (!mux.RescaleTime(pkt, video_begin_pts, demux.video_time_base())) {
        av_packet_unref(pkt);
        continue;
      }
      video_frame_count++;

      // pkt->duration 已被 RescaleTime 换算成输出流 time_base
      // 注意：这里统计的是实际写入的帧时长，包含 Seek 落在 begin 之前的关键帧（GOP 预波）
      if (demux.video_time_base().den > 0) {
        total_sec += pkt->duration *
                     av_q2d(mux_c->streams[pkt->stream_index]->time_base);
      }
    } else if (is_audio) {
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

    // 写入音视频帧 会清理 pkt
    if (!mux.Write(pkt))
      break;
  }

  // 写入 尾部trailer
  // MP4 的 moov 索引头是在写入文件尾时才正式生成并刷入磁盘
  mux.WriteEnd();

  // 释放 pkt
  av_packet_free(&pkt);

  // 清理全局上下文
  mux.set_c(nullptr);
  demux.set_c(nullptr);
  std::cout << "\n输出文件：" << out_file << '\n';
  std::cout << "视频帧：" << video_frame_count << '\n';
  std::cout << "音频帧：" << audio_frame_count << '\n';
  std::cout << "总时长：" << total_sec << " 秒\n";
  if (read_error)
    std::cerr << "警告：读取源文件时发生错误，输出可能不完整" << std::endl;

  return EXIT_SUCCESS;
}
