// ============================================================================
//  frame.cpp —— Frame 的实现
// ============================================================================

#include "detail/ffi.h"

namespace xencode {

using detail::make_error;

// ---------------------------------------------------------------------------
// PlaneView
// ---------------------------------------------------------------------------

std::span<std::uint8_t> Frame::PlaneView::row(int y) noexcept {
  // 边界检查放在这里：越界返回空 span，而不是给一个越界指针。
  // 原版直接 frame->data[1][y * linesize + x] 裸算，写错了就是 SIGSEGV。
  if (y < 0 || y >= height || stride <= 0)
    return {};
  const auto offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
  const auto width_n = static_cast<std::size_t>(width);
  if (offset + width_n > pixels.size())
    return {};
  return pixels.subspan(offset, width_n);
}

std::span<const std::uint8_t> Frame::PlaneView::row(int y) const noexcept {
  if (y < 0 || y >= height || stride <= 0)
    return {};
  const auto offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
  const auto width_n = static_cast<std::size_t>(width);
  if (offset + width_n > pixels.size())
    return {};
  return std::span<const std::uint8_t>(pixels.data() + offset, width_n);
}

// ---------------------------------------------------------------------------
// Frame 生命周期
// ---------------------------------------------------------------------------

Frame::Frame() noexcept = default;
Frame::~Frame() = default;

Frame::Frame(Frame &&) noexcept = default;
Frame &Frame::operator=(Frame &&) noexcept = default;

Frame::Frame(std::unique_ptr<detail::FrameImpl> impl) noexcept
    : impl_(std::move(impl)) {}

bool Frame::valid() const noexcept {
  return impl_ && impl_->raw && impl_->raw->data[0] != nullptr;
}

int Frame::width() const noexcept {
  return (impl_ && impl_->raw) ? impl_->raw->width : 0;
}

int Frame::height() const noexcept {
  return (impl_ && impl_->raw) ? impl_->raw->height : 0;
}

Result<void> Frame::make_writable() {
  if (!valid())
    return std::unexpected(
        make_error(ErrorCode::invalid_frame, 0, "frame is empty"));

  // 返回值必须检查 —— 原版这一步是直接忽略的。
  const int ret = av_frame_make_writable(impl_->raw.get());
  if (ret < 0)
    return std::unexpected(make_error(ErrorCode::frame_not_writable, ret));
  return {};
}

void Frame::set_pts(std::int64_t pts) noexcept {
  if (impl_ && impl_->raw)
    impl_->raw->pts = pts;
}

std::int64_t Frame::pts() const noexcept {
  return (impl_ && impl_->raw) ? impl_->raw->pts : 0;
}

// ---------------------------------------------------------------------------
// Frame::plane —— 整个设计的核心：平面自带正确尺寸
// ---------------------------------------------------------------------------

Result<Frame::PlaneView> Frame::plane(Plane p) noexcept {
  if (!valid())
    return std::unexpected(
        make_error(ErrorCode::invalid_frame, 0, "frame is empty"));

  AVFrame *raw = impl_->raw.get();
  const int index = static_cast<int>(p);

  if (index < 0 || index > 3 || raw->data[index] == nullptr)
    return std::unexpected(make_error(
        ErrorCode::invalid_frame, 0, "requested plane does not exist in frame"));

  int plane_w = 0;
  int plane_h = 0;
  if (!detail::plane_dimensions(raw, index, &plane_w, &plane_h))
    return std::unexpected(make_error(ErrorCode::invalid_frame, 0,
                                      "unsupported pixel format for plane dims"));

  const int linesize = raw->linesize[index];
  if (linesize <= 0)
    return std::unexpected(make_error(
        ErrorCode::invalid_frame, 0, "negative line size is not supported"));

  PlaneView view;
  // pixels 覆盖整块平面（含 padding），row() 再把每行裁到 width。
  view.pixels = std::span<std::uint8_t>(
      raw->data[index],
      static_cast<std::size_t>(linesize) * static_cast<std::size_t>(plane_h));
  view.stride = linesize;
  view.width = plane_w;   // ← U/V 这里是 width/2
  view.height = plane_h;  // ← U/V 这里是 height/2

  return view;
}

} // namespace xencode
