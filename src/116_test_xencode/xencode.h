#pragma once

#include <mutex>
#include <vector>

// forward definition
struct AVFrame;
struct AVPacket;
struct AVCodec;
struct AVCodecContext;

class XEncode {
public:
  ~XEncode(); // 增加析构函数

  ///////////////////////////////////////////////
  /// 创建编码上下文
  /// 与成员无关 static
  /// @para codec_id 编码器 ID号，对应 FFmpeg
  /// @return 编码上下文，失败返回 nullptr
  ///////////////////////////////////////////////
  static AVCodecContext *Create(int codec_id);

  ///////////////////////////////////////////////
  /// 设置对象的编码器上下文，上下文传递到对象中，
  /// 空间由 XEncode维护
  /// @para c 编码器上下文，如果 c_ 不为 nullptr，
  /// 则先清理资源
  ///////////////////////////////////////////////
  void set_c(AVCodecContext *c);

  ///////////////////////////////////////////////
  /// 设置编码参数，线程安全
  /// C++ 支持函数重载
  /// @return 是否正确添加参数
  ///////////////////////////////////////////////
  bool SetOpt(const char *key, const char *value);
  bool SetOpt(const char *key, int value);

  ///////////////////////////////////////////////
  /// 打开编码器 线程安全
  ///////////////////////////////////////////////
  bool Open();

  ///////////////////////////////////////////////
  /// 编码数据 线程安全 每次新创建AVPacket
  /// @para frame 空间由用户维护
  /// @return 失败返回一个空 vector
  /// 返回的 std::vector<AVPacket*>
  /// av_packet_free() 清理
  ///////////////////////////////////////////////
  std::vector<AVPacket *> Encode(const AVFrame *frame);

  ///////////////////////////////////////////////
  /// 创建一个 AVFrame，根据 AVCodecContext，需要
  /// 调用者释放
  ///////////////////////////////////////////////
  AVFrame *CreateFrame();

  ///////////////////////////////////////////////
  /// 返回所有编码缓冲中的 AVPacket
  ///////////////////////////////////////////////
  std::vector<AVPacket *> End();

private:
  AVCodecContext *c_ = nullptr;
  std::mutex mtx_;
};
