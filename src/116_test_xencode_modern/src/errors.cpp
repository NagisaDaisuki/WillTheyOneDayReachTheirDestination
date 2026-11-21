// ============================================================================
//  errors.cpp —— 错误码文本、配置校验
// ============================================================================

#include "detail/ffi.h"

#include <cstdio>

namespace xencode {

// 内部错误构造工具（定义在 detail/ffi.h）
using detail::make_error;

std::string_view to_string(Codec codec) noexcept {
  switch (codec) {
  case Codec::h264:
    return "h264";
  case Codec::hevc:
    return "hevc";
  }
  return "unknown";
}

std::string_view to_string(RateControl rc) noexcept {
  switch (rc) {
  case RateControl::constant_quality:
    return "constant_quality";
  case RateControl::constant_quality_capped:
    return "constant_quality_capped";
  case RateControl::constant_bitrate:
    return "constant_bitrate";
  case RateControl::average_bitrate:
    return "average_bitrate";
  }
  return "unknown";
}

std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
  case ErrorCode::invalid_config:
    return "invalid_config";
  case ErrorCode::codec_not_found:
    return "codec_not_found";
  case ErrorCode::context_alloc_failed:
    return "context_alloc_failed";
  case ErrorCode::option_rejected:
    return "option_rejected";
  case ErrorCode::open_failed:
    return "open_failed";
  case ErrorCode::frame_alloc_failed:
    return "frame_alloc_failed";
  case ErrorCode::frame_not_writable:
    return "frame_not_writable";
  case ErrorCode::invalid_frame:
    return "invalid_frame";
  case ErrorCode::send_failed:
    return "send_failed";
  case ErrorCode::receive_failed:
    return "receive_failed";
  case ErrorCode::flush_failed:
    return "flush_failed";
  case ErrorCode::already_flushed:
    return "already_flushed";
  case ErrorCode::io_failed:
    return "io_failed";
  }
  return "unknown";
}

std::string describe(const Error &error) {
  std::string text{to_string(error.code)};

  if (error.ffmpeg_error != 0) {
    // av_strerror 需要一块外部缓冲区，用 std::array 包一下更安全。
    char buffer[256] = {0};
    if (av_strerror(error.ffmpeg_error, buffer, sizeof(buffer)) < 0)
      std::snprintf(buffer, sizeof(buffer), "AVERROR(%d)", error.ffmpeg_error);
    text += ": ";
    text += buffer;
  }

  if (!error.detail.empty()) {
    text += " (";
    text += error.detail;
    text += ')';
  }
  return text;
}

Result<void> EncoderConfig::validate() const {
  if (width <= 0 || height <= 0)
    return std::unexpected(
        make_error(ErrorCode::invalid_config, 0, "width/height must be > 0"));

  // yuv420p 要求宽高为偶数，否则色度平面会有一半像素没有对应（各编码器
  // 处理方式不一致，不如直接拒绝）。原版封装对这一点完全没有检查。
  if ((width % 2) != 0 || (height % 2) != 0)
    return std::unexpected(make_error(
        ErrorCode::invalid_config, 0, "width/height must be even for yuv420p"));

  if (fps <= 0)
    return std::unexpected(
        make_error(ErrorCode::invalid_config, 0, "fps must be > 0"));

  if (bitrate_kbps <= 0)
    return std::unexpected(
        make_error(ErrorCode::invalid_config, 0, "bitrate_kbps must be > 0"));

  if (buffer_kbps <= 0)
    return std::unexpected(
        make_error(ErrorCode::invalid_config, 0, "buffer_kbps must be > 0"));

  if (constant_quality < 0)
    return std::unexpected(
        make_error(ErrorCode::invalid_config, 0, "constant_quality must be >= 0"));

  return {};
}

} // namespace xencode
