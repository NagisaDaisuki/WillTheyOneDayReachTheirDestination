#pragma once
extern "C" {
#include <libavcodec/avcodec.h>
}
#include <map>
#include <mutex>

// 前置声明
struct AVFormat;
struct AVFormatContext;
struct AVCodecParameters;
struct AVPacket;
struct AVCodecContext;

struct XRational { // 1:1 复刻
  int num;         // Numerator
  int den;         // Denominator
};

void PrintErr(int err);
#define BERR(err)                                                              \
  if (err != 0) {                                                              \
    PrintErr(err);                                                             \
    return 0;                                                                  \
  } // 返回0 false

//////////////////////////////////////////////////////////////
/// 封装和解封装的基类
//////////////////////////////////////////////////////////////
class XFormat {
public:
  //////////////////////////////////////////////
  /// 参数复制函数
  //////////////////////////////////////////////
  bool CopyPara(int stream_index, AVCodecParameters *dst);
  bool CopyPara(int stream_index, AVCodecContext *dst);

  //////////////////////////////////////////////
  /// 设置上下文并清理上次设置的值
  /// 传递 NULL 相当于关闭上下文
  /// 线程安全
  //////////////////////////////////////////////
  void set_c(AVFormatContext *c);

  //////////////////////////////////////////////
  /// 设置 输入流索引 -> 输出流索引 映射
  /// 封装时输入的 stream_index 和输出的 stream_index 不保证一致，
  /// 必须显式映射，否则会把数据写进错误的流
  //////////////////////////////////////////////
  void SetStreamMap(int in_index, int out_index);

  //////////////////////////////////////////////
  /// 把输入流索引换算成本上下文的流索引，非法返回 -1
  /// 注意：内部不加锁，调用者需保证线程安全
  //////////////////////////////////////////////
  int OutStreamIndex(int in_index) const;

  //////////////////////////////////////////////
  /// 根据 timebase换算时间
  /// pkt->stream_index 为输入流索引，内部会转换成输出流索引并回写
  //////////////////////////////////////////////
  bool RescaleTime(AVPacket *pkt, long long offset_pts, XRational time_base);

  int audio_index() { return audio_index_; }
  int video_index() { return video_index_; }
  XRational video_time_base() { return video_time_base_; }
  XRational audio_time_base() { return audio_time_base_; }
  int video_codec_id() { return video_codec_id_; }

protected:
  // 封装解封装上下文
  AVFormatContext *c_{nullptr};
  // 针对c_上下文资源 互斥访问
  std::mutex mux_{};
  // video audio 在 stream 中的索引
  int video_index_{0};
  int audio_index_{1};
  // 视频编码器ID
  int video_codec_id_{0};
  // 视频音频时间基
  XRational video_time_base_{1, 25};
  XRational audio_time_base_{1, 9000};
  // 输入流索引 -> 输出流索引 (未设置的索引按 1:1 处理)
  std::map<int, int> stream_map_{};
};
