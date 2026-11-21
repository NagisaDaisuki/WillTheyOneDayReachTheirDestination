// ============================================================================
//  bitstream_writer.cpp —— Annex-B 裸流写文件
//
//  把 ofstream 也藏进 pImpl，调用方完全看不到文件和流状态。
// ============================================================================

#include "detail/ffi.h"

#include <fstream>

namespace xencode {

using detail::make_error;

struct BitstreamWriter::Impl {
  std::filesystem::path path;
  std::ofstream out;
  std::uint64_t packet_count = 0;
  bool closed = false;
};

BitstreamWriter::BitstreamWriter(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

BitstreamWriter::~BitstreamWriter() = default;
BitstreamWriter::BitstreamWriter(BitstreamWriter &&) noexcept = default;
BitstreamWriter &BitstreamWriter::operator=(BitstreamWriter &&) noexcept =
    default;

Result<BitstreamWriter> BitstreamWriter::create(std::filesystem::path path) {
  auto impl = std::make_unique<Impl>();
  impl->path = std::move(path);
  impl->out.open(impl->path, std::ios::binary | std::ios::trunc);
  if (!impl->out.is_open())
    return std::unexpected(make_error(ErrorCode::io_failed, 0,
                                      "cannot open " + impl->path.string()));
  return BitstreamWriter(std::move(impl));
}

Result<void> BitstreamWriter::write(const Packet &packet) {
  if (!impl_ || impl_->closed)
    return std::unexpected(
        make_error(ErrorCode::io_failed, 0, "writer is closed"));

  if (!packet.valid())
    return std::unexpected(
        make_error(ErrorCode::invalid_frame, 0, "empty packet"));

  const std::span<const std::uint8_t> bytes = packet.data();
  if (bytes.empty())
    return {}; // 空包（例如纯参数集占位）不算错误

  impl_->out.write(reinterpret_cast<const char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
  if (!impl_->out)
    return std::unexpected(
        make_error(ErrorCode::io_failed, 0, "write failed"));

  ++impl_->packet_count;
  return {};
}

Result<void> BitstreamWriter::write_all(std::span<const Packet> packets) {
  for (const auto &packet : packets) {
    if (auto written = write(packet); !written)
      return written;
  }
  return {};
}

std::uint64_t BitstreamWriter::packet_count() const noexcept {
  return impl_ ? impl_->packet_count : 0;
}

Result<void> BitstreamWriter::close() {
  if (!impl_)
    return std::unexpected(
        make_error(ErrorCode::io_failed, 0, "writer is empty"));
  if (impl_->closed)
    return {};

  impl_->out.flush();
  const bool healthy = static_cast<bool>(impl_->out);
  impl_->out.close();
  impl_->closed = true;

  if (!healthy)
    return std::unexpected(
        make_error(ErrorCode::io_failed, 0, "flush failed"));
  return {};
}

} // namespace xencode
