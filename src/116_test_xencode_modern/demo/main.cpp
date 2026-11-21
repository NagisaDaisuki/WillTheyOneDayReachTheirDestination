// ============================================================================
//  demo/main.cpp —— 用现代接口跑和原版完全一样的任务
//
//  请先注意这个文件里 **没有任何 libav* 的 include**，也没有 extern "C"，
//  更没有 av_frame_alloc / av_packet_free / av_opt_set。
//  调用方只需要理解 Frame / Packet / Encoder 三个值类型。
// ============================================================================

#include <xencode/xencode.h>

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr int kWidth = 1920;
constexpr int kHeight = 1080;
constexpr int kFps = 25;
constexpr int kFrameCount = 1000;

/// 打印错误并返回进程退出码，避免每个失败分支都写三行。
int report(const xencode::Error &error, std::string_view stage) {
  std::cerr << "[" << stage << "] " << xencode::describe(error) << '\n';
  return 1;
}

/// 生成和旧版本 116_test_xencode.cpp 逐字节相同的测试图，
/// 这样两版输出的 .264 可以直接 cmp 对比。
xencode::Result<void> fill_test_pattern(xencode::Frame &frame, int index) {
  auto fill = [index](xencode::Frame::PlaneView view, bool use_y) {
    for (int y = 0; y < view.height; ++y) {
      // row() 自带边界检查，且长度就是 view.width ——
      // 旧版那个"用亮度宽高写色度平面"的越界 bug 在这里写不出来：
      // view.width / view.height 对 U/V 平面自动就是 width/2、height/2。
      std::span<std::uint8_t> line = view.row(y);
      for (int x = 0; x < view.width; ++x) {
        const int value = use_y ? (x + y + index * 3) : (128 + y + index * 2);
        line[x] = static_cast<std::uint8_t>(value);
      }
    }
  };

  auto luma = frame.plane(xencode::Plane::y);
  if (!luma)
    return std::unexpected(luma.error());
  fill(*luma, true);

  auto chroma_u = frame.plane(xencode::Plane::u);
  if (!chroma_u)
    return std::unexpected(chroma_u.error());
  fill(*chroma_u, false);

  auto chroma_v = frame.plane(xencode::Plane::v);
  if (!chroma_v)
    return std::unexpected(chroma_v.error());
  for (int y = 0; y < chroma_v->height; ++y) {
    std::span<std::uint8_t> line = chroma_v->row(y);
    for (int x = 0; x < chroma_v->width; ++x)
      line[x] = static_cast<std::uint8_t>(64 + x + index * 2);
  }

  return {};
}

xencode::EncoderConfig make_config(xencode::Codec codec, int threads) {
  xencode::EncoderConfig config;
  config.codec = codec;
  config.width = kWidth;
  config.height = kHeight;
  config.fps = kFps;
  config.rate_control = xencode::RateControl::constant_quality_capped;
  config.constant_quality = 18;
  config.bitrate_kbps = 800;
  config.buffer_kbps = 1600;
  // 0 = 交给 FFmpeg 决定。注意 threads > 1 时 x264 的输出**不可复现**
  // （同一二进制连续跑两次的 md5 都不一样），想逐字节比对产物请用 1。
  config.thread_count = threads;
  return config;
}

std::string make_filename(const xencode::EncoderConfig &config) {
  std::string name = std::to_string(config.width) + "_" +
                     std::to_string(config.height) + "_" +
                     std::to_string(config.fps) + "_" +
                     std::string{xencode::to_string(config.codec)} + "_" +
                     std::string{xencode::to_string(config.rate_control)};
  name += (config.codec == xencode::Codec::h264) ? ".264" : ".265";
  return name;
}

int run(xencode::Codec codec, int threads) {
  const xencode::EncoderConfig config = make_config(codec, threads);
  const std::string filename = make_filename(config);

  std::cout << "codec=" << xencode::to_string(config.codec)
            << " rate_control=" << xencode::to_string(config.rate_control)
            << " threads=" << config.thread_count << " -> " << filename << '\n';

  // ---- 打开编码器：配置(builder) 与 编码(encoder) 是两种类型 ----
  auto encoder = xencode::make_encoder(config);
  if (!encoder)
    return report(encoder.error(), "make_encoder");

  auto writer = xencode::BitstreamWriter::create(filename);
  if (!writer)
    return report(writer.error(), "open output");

  // ---- 分配一帧并复用（比每帧新分配更贴近真实用法）----
  auto frame = encoder->create_frame();
  if (!frame)
    return report(frame.error(), "create_frame");

  std::cout << "开始编码..." << std::endl;

  for (int index = 0; index < kFrameCount; ++index) {
    // 复用同一块缓冲区时必须先确认可写；返回值一定要检查。
    if (auto writable = frame->make_writable(); !writable)
      return report(writable.error(), "make_writable");

    if (auto filled = fill_test_pattern(*frame, index); !filled)
      return report(filled.error(), "fill");

    frame->set_pts(index);

    auto packets = encoder->encode(*frame);
    if (!packets)
      return report(packets.error(), "encode");

    if (auto written = writer->write_all(*packets); !written)
      return report(written.error(), "write");
  }

  // ---- 冲刷尾部缓存 ----
  auto tail = encoder->flush();
  if (!tail)
    return report(tail.error(), "flush");

  if (auto written = writer->write_all(*tail); !written)
    return report(written.error(), "write tail");

  if (auto closed = writer->close(); !closed)
    return report(closed.error(), "close");

  std::cout << "Total Packet Encoded: " << writer->packet_count() << std::endl;
  return 0;
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    xencode::Codec codec = xencode::Codec::h264;
    if (argc > 1) {
      const std::string_view arg{argv[1]};
      if (arg == "h265" || arg == "hevc")
        codec = xencode::Codec::hevc;
      else if (arg != "h264")
        std::cerr << "unknown codec '" << arg << "', falling back to h264\n";
    }
    int threads = 16;
    if (argc > 2)
      threads = std::stoi(argv[2]);

    return run(codec, threads);
  } catch (const std::exception &ex) {
    std::cerr << "unhandled exception: " << ex.what() << '\n';
    return 1;
  }
}
