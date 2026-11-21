#pragma once
#include "xcodec.h"
#include <vector>

class XEncode : public XCodec {
public:
  ///////////////////////////////////////////////
  /// 编码数据 线程安全 每次新创建AVPacket
  /// @para frame 空间由用户维护
  /// @return 失败返回一个空 vector
  /// 返回的 std::vector<AVPacket*>
  /// av_packet_free() 清理
  ///////////////////////////////////////////////
  std::vector<AVPacket *> Encode(const AVFrame *frame);

  ///////////////////////////////////////////////
  /// 返回所有编码缓冲中的 AVPacket
  ///////////////////////////////////////////////
  std::vector<AVPacket *> End();
};
