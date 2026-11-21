// ============================================================================
//  detail/ffi.h —— 内部头文件（**不对外安装、不对外暴露**）
//
//  这是整个项目里唯一 include <libavcodec/...> 的地方。
//  公开头文件 include/xencode/xencode.h 对它一无所知。
//
//  除了 FFmpeg 头文件，这里还定义了三个 pImpl 的具体内容，以及唯一一个
//  可以触碰裸 AVPacket* / AVFrame* / AVCodecContext* 的"特权接口" FfiAccess。
//  FfiAccess 是 public 头文件里声明过的 friend，所以只有它能拿到底层指针。
// ============================================================================

#pragma once

#include <xencode/xencode.h>

#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

namespace xencode::detail {

// ---------------------------------------------------------------------------
// 带删除器的 unique_ptr —— FFmpeg 对象的 RAII 底座
//
// 注意删除器接受的是指针的**值**，因为 av_xxx_free() 需要 T** 并把它置空，
// 而 unique_ptr 在 deleter 之后就会丢弃这个指针，所以传值即可。
// ---------------------------------------------------------------------------

struct CtxDeleter {
  void operator()(AVCodecContext *ctx) const noexcept {
    avcodec_free_context(&ctx);
  }
};

struct FrameDeleter {
  void operator()(AVFrame *frame) const noexcept { av_frame_free(&frame); }
};

struct PacketDeleter {
  void operator()(AVPacket *packet) const noexcept { av_packet_free(&packet); }
};

using CtxPtr = std::unique_ptr<AVCodecContext, CtxDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

// ---------------------------------------------------------------------------
// pImpl 的实际定义
// ---------------------------------------------------------------------------

struct FrameImpl {
  FramePtr raw;

  FrameImpl() = default;
  explicit FrameImpl(AVFrame *frame) noexcept : raw(frame) {}

  FrameImpl(FrameImpl &&) = default;
  FrameImpl &operator=(FrameImpl &&) = default;
  FrameImpl(const FrameImpl &) = delete;
  FrameImpl &operator=(const FrameImpl &) = delete;
};

struct PacketImpl {
  PacketPtr raw;

  PacketImpl() = default;
  explicit PacketImpl(AVPacket *packet) noexcept : raw(packet) {}

  PacketImpl(PacketImpl &&) = default;
  PacketImpl &operator=(PacketImpl &&) = default;
  PacketImpl(const PacketImpl &) = delete;
  PacketImpl &operator=(const PacketImpl &) = delete;
};

struct EncoderImpl {
  CtxPtr ctx;
  EncoderConfig config;

  /// flush() 之后不再接受新帧。用显式状态机代替"靠 AVERROR_EOF 静默兜底"。
  bool flushed = false;

  EncoderImpl() = default;
  explicit EncoderImpl(CtxPtr context, EncoderConfig cfg)
      : ctx(std::move(context)), config(cfg) {}
};

// ---------------------------------------------------------------------------
// FfiAccess —— 唯一可以造/取裸指针的特权结构
//
// 公开头文件里 Frame / Packet 把它声明成了 friend，所以：
//   * 只有本项目内部代码（那些 include 了本文件的 .cpp）能拿到 AVPacket*；
//   * 调用方即使拿到 xencode.h 也做不到。
// ---------------------------------------------------------------------------

struct FfiAccess {
  static AVFrame *raw(Frame &frame) noexcept {
    return frame.impl_ ? frame.impl_->raw.get() : nullptr;
  }
  static const AVFrame *raw(const Frame &frame) noexcept {
    return frame.impl_ ? frame.impl_->raw.get() : nullptr;
  }

  static AVPacket *raw(Packet &packet) noexcept {
    return packet.impl_ ? packet.impl_->raw.get() : nullptr;
  }
  static const AVPacket *raw(const Packet &packet) noexcept {
    return packet.impl_ ? packet.impl_->raw.get() : nullptr;
  }

  static AVCodecContext *raw(Encoder &encoder) noexcept {
    return encoder.impl_ ? encoder.impl_->ctx.get() : nullptr;
  }

  /// 接管一个已分配的 AVFrame（所有权转移，调用方不要再 free）。
  static Frame adopt(AVFrame *frame) noexcept {
    return Frame(std::make_unique<FrameImpl>(frame));
  }

  /// 接管一个已分配的 AVPacket（所有权转移）。
  static Packet adopt(AVPacket *packet) noexcept {
    return Packet(std::make_unique<PacketImpl>(packet));
  }
};

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------

/// 由 AVERROR 值构造 Error。
inline Error make_error(ErrorCode code, int ffmpeg_error = 0,
                        std::string detail = {}) {
  return Error{code, ffmpeg_error, std::move(detail)};
}

/// 按像素格式折算某个平面的有效宽高。
/// yuv420p 的 U/V 会是 width/2 × height/2 —— 这正是原版 bug 的根源，
/// 在这里统一算出来，上层就不需要（也无法）自己猜。
inline bool plane_dimensions(const AVFrame *frame, int plane_index, int *out_w,
                             int *out_h) noexcept {
  if (!frame || !out_w || !out_h)
    return false;

  const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(
      static_cast<AVPixelFormat>(frame->format));
  if (!desc)
    return false;

  const int chroma_w = (plane_index == 0) ? 0 : desc->log2_chroma_w;
  const int chroma_h = (plane_index == 0) ? 0 : desc->log2_chroma_h;

  *out_w = (frame->width + (1 << chroma_w) - 1) >> chroma_w;
  *out_h = (frame->height + (1 << chroma_h) - 1) >> chroma_h;
  return true;
}

} // namespace xencode::detail
