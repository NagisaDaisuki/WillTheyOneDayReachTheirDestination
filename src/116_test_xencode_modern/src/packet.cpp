// ============================================================================
//  packet.cpp —— Packet 的实现
// ============================================================================

#include "detail/ffi.h"

namespace xencode {

Packet::Packet() noexcept = default;
Packet::~Packet() = default;

Packet::Packet(Packet &&) noexcept = default;
Packet &Packet::operator=(Packet &&) noexcept = default;

Packet::Packet(std::unique_ptr<detail::PacketImpl> impl) noexcept
    : impl_(std::move(impl)) {}

bool Packet::valid() const noexcept {
  return impl_ && impl_->raw;
}

std::span<const std::uint8_t> Packet::data() const noexcept {
  if (!valid() || impl_->raw->data == nullptr || impl_->raw->size <= 0)
    return {};
  return std::span<const std::uint8_t>(
      impl_->raw->data, static_cast<std::size_t>(impl_->raw->size));
}

bool Packet::is_key_frame() const noexcept {
  return valid() && (impl_->raw->flags & AV_PKT_FLAG_KEY) != 0;
}

std::int64_t Packet::pts() const noexcept {
  return valid() ? impl_->raw->pts : 0;
}

std::int64_t Packet::dts() const noexcept {
  return valid() ? impl_->raw->dts : 0;
}

} // namespace xencode
