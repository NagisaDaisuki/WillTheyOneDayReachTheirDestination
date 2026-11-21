// ============================================================================
//  xencode —— 一个现代 C++ 风格的 FFmpeg 编码器封装
//
//  设计目标（对比 ../116_test_xencode/ 的原版）：
//
//   1. 【零 FFmpeg 泄漏】本头文件里不出现任何 libav* 的类型、宏或头文件。
//      调用方不需要 include <libavcodec/avcodec.h>，也不用 extern "C"。
//      所有 FFmpeg 细节都在 src/ 里的 pImpl 实现中。
//
//   2. 【无裸指针进出接口】Frame / Packet / Encoder 全是值语义 + move-only 的
//      RAII 对象。调用方既收不到裸指针，也不需要"记得调用 av_xxx_free()"。
//
//   3. 【错误用类型表达】用 std::expected<T, Error> 代替
//      "失败返回 nullptr" / "失败返回空 vector" / "errno 风格的 int"。
//      "没有数据" 和 "出错了" 在类型上是两件不同的事。
//
//   4. 【调用顺序编译期可查】EncoderBuilder（只能配置）→ open() → Encoder
//      （只能编码）。把 "SetOpt 必须在 Open 之前" 这种隐式契约变成类型约束，
//      写错根本编译不过，不需要靠注释和 mutex 兜底。
//
//   5. 【越界不可能发生】Frame::plane() 返回的 PlaneView **自带正确的宽高**
//      （U/V 已经是 width/2 × height/2），并通过 row() 做边界检查。
//      原版那个 c->height / c->width 写色度平面导致 SIGSEGV 的 bug，
//      在这套接口下写不出来。
//
//   6. 【不丢帧】encode() 内部完整处理 AVERROR(EAGAIN)，绝不静默丢帧。
//
//   7. 【线程模型说清楚】Encoder 明确声明 "非线程安全"，不再用一把大锁把
//      FFmpeg 调用整个串起来假装线程安全。
//
//  需要 C++23（std::expected / std::span）。
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xencode {

// ============================================================================
// 基本枚举
// ============================================================================

/// 支持的视频编码器。新增编码器只需在这里加一项 + 在 encoder.cpp 的
/// policy 表里加一行，调用方代码不用改（不需要 switch / dynamic_cast）。
enum class Codec {
  h264,
  hevc,
};

/// 码率控制方式。
enum class RateControl {
  constant_quality,         ///< 纯 CRF/VBR（点播首选，体积不可控）
  constant_quality_capped,  ///< CRF + VBV 上限（高质量在线点播，B站/YouTube 常用）
  constant_bitrate,         ///< CBR（直播推流，带填充）
  average_bitrate,          ///< ABR（硬凑目标码率，画质波动大）
};

/// 图像平面。U/V 的尺寸由 Frame::plane() 自动按像素格式折算。
enum class Plane {
  y = 0,
  u = 1,
  v = 2,
};

std::string_view to_string(Codec codec) noexcept;
std::string_view to_string(RateControl rc) noexcept;

// ============================================================================
// 错误
// ============================================================================

enum class ErrorCode {
  invalid_config,
  codec_not_found,
  context_alloc_failed,
  option_rejected,
  open_failed,
  frame_alloc_failed,
  frame_not_writable,
  invalid_frame,
  send_failed,
  receive_failed,
  flush_failed,
  already_flushed,
  io_failed,
};

/// 错误信息。ffmpeg_error 保留 AVERROR() 原始值，便于查文档；
/// detail 是给人看的说明。
struct Error {
  ErrorCode code = ErrorCode::invalid_config;
  int ffmpeg_error = 0;
  std::string detail;
};

std::string_view to_string(ErrorCode code) noexcept;

/// 生成一行完整可读的错误描述（含 av_strerror 文本）。
std::string describe(const Error &error);

/// 所有可能失败的返回值都用它。Result<void> 表示"要么成功要么有错"。
template <class T>
using Result = std::expected<T, Error>;

// ============================================================================
// 前置声明（只有名字，不含定义 —— 这就是 pImpl）
// ============================================================================

namespace detail {
struct FrameImpl;
struct PacketImpl;
struct EncoderImpl;
struct FfiAccess;
} // namespace detail

// ============================================================================
// EncoderConfig —— 一次性把编码参数描述清楚，替代"边 set 边改结构体字段"
// ============================================================================

struct EncoderConfig {
  Codec codec = Codec::h264;

  int width = 0;
  int height = 0;
  int fps = 25;

  /// 时间基分母固定为 fps，即 time_base = 1/fps。
  RateControl rate_control = RateControl::constant_quality_capped;

  /// CRF / CQP 的量化参数：越小画质越好、体积越大。18 接近"视觉无损"。
  int constant_quality = 18;

  /// 目标码率（kbps）。ABR/CBR 是目标值，capped 模式是上限值。
  int bitrate_kbps = 800;

  /// VBV 缓冲区（kbps）。一般取目标码率的 1~2 倍。
  int buffer_kbps = 1600;

  /// 0 = 让 FFmpeg 自己决定；>0 时强制指定线程数。
  int thread_count = 0;

  /// 校验参数是否自洽（尺寸为正、宽高为偶数等）。
  Result<void> validate() const;
};

// ============================================================================
// Frame —— 一帧 YUV 图像
// ============================================================================

class Frame {
public:
  /// 一个平面的可写视图。
  ///
  /// 关键设计：width / height **已经是该平面自己的尺寸**。
  /// 对 yuv420p 的 U/V 而言就是 width/2 × height/2。
  /// 调用方用 view.height / view.width 做循环上界，天然不可能写越界。
  struct PlaneView {
    /// 整块可写内存，长度 = stride * height（含行尾 padding）。
    std::span<std::uint8_t> pixels;
    /// 相邻两行首字节的距离（字节）。
    int stride = 0;
    /// 有效像素宽度（不含 padding；U/V 已折半）。
    int width = 0;
    /// 有效像素高度（U/V 已折半）。
    int height = 0;

    /// 取第 y 行前 width 个可写字节；y 越界时返回空 span（不会 UB）。
    std::span<std::uint8_t> row(int y) noexcept;
    std::span<const std::uint8_t> row(int y) const noexcept;
  };

  Frame() noexcept;
  ~Frame();
  Frame(Frame &&) noexcept;
  Frame &operator=(Frame &&) noexcept;
  Frame(const Frame &) = delete;
  Frame &operator=(const Frame &) = delete;

  bool valid() const noexcept;

  int width() const noexcept;
  int height() const noexcept;

  /// 确保底层缓冲区可写。复用 Frame 时必须在写入前调用。
  Result<void> make_writable();

  /// 取某个平面的可写视图。
  Result<PlaneView> plane(Plane p) noexcept;

  void set_pts(std::int64_t pts) noexcept;
  std::int64_t pts() const noexcept;

private:
  friend struct detail::FfiAccess;
  explicit Frame(std::unique_ptr<detail::FrameImpl> impl) noexcept;

  std::unique_ptr<detail::FrameImpl> impl_;
};

// ============================================================================
// Packet —— 一个压缩后的码流包
// ============================================================================

class Packet {
public:
  Packet() noexcept;
  ~Packet();
  Packet(Packet &&) noexcept;
  Packet &operator=(Packet &&) noexcept;
  Packet(const Packet &) = delete;
  Packet &operator=(const Packet &) = delete;

  bool valid() const noexcept;

  /// 只读码流数据视图（长度 = size）。
  std::span<const std::uint8_t> data() const noexcept;

  bool is_key_frame() const noexcept;
  std::int64_t pts() const noexcept;
  std::int64_t dts() const noexcept;

private:
  friend struct detail::FfiAccess;
  explicit Packet(std::unique_ptr<detail::PacketImpl> impl) noexcept;

  std::unique_ptr<detail::PacketImpl> impl_;
};

// ============================================================================
// Encoder —— 已打开、可编码的编码器
//
//  不提供 set_option()：参数只能在 EncoderBuilder 阶段给。
//  线程安全：**不是**线程安全的。AVCodecContext 的 send/receive 并发调用是 UB。
//            多线程场景请每个线程持有独立的 Encoder 实例。
// ============================================================================

class Encoder {
public:
  ~Encoder();
  Encoder(Encoder &&) noexcept;
  Encoder &operator=(Encoder &&) noexcept;
  Encoder(const Encoder &) = delete;
  Encoder &operator=(const Encoder &) = delete;

  const EncoderConfig &config() const noexcept;

  /// 按编码器配置分配一帧（已调用 av_frame_get_buffer）。
  Result<Frame> create_frame() const;

  /// 编码一帧。
  /// @return 本次调用产生的 0..N 个包；可能为空 vector（编码器内部还有缓存）。
  ///         失败时返回 Error —— "没有包" 和 "出错" 是两种不同的返回。
  /// 内部完整处理 AVERROR(EAGAIN)：先抽干输出队列再重试发送，绝不丢帧。
  Result<std::vector<Packet>> encode(const Frame &frame);

  /// 冲刷编码器内部缓存的帧，返回剩余所有包。只能调用一次。
  Result<std::vector<Packet>> flush();

private:
  friend class EncoderBuilder;
  friend struct detail::FfiAccess;
  explicit Encoder(std::unique_ptr<detail::EncoderImpl> impl) noexcept;

  std::unique_ptr<detail::EncoderImpl> impl_;
};

// ============================================================================
// EncoderBuilder —— 配置阶段
//
//  这是访问编码器的唯一入口：
//      auto enc = EncoderBuilder{cfg}.set_option("preset", "fast").open();
//  配置完才能 open()，open() 之后拿到的是没有 set_option() 的 Encoder，
//  于是 "av_opt_set 必须在 avcodec_open2 之前" 这条规则由类型系统保证。
// ============================================================================

class EncoderBuilder {
public:
  explicit EncoderBuilder(EncoderConfig config);

  /// 设置编码器私有/上下文选项（透传给 av_opt_set，等价的字符串写法）。
  /// 例：set_option("preset", "fast")、set_option("tune", "zerolatency")
  EncoderBuilder &set_option(std::string_view key, std::string_view value);

  /// 设置整数选项，例：set_option("qp", 23)
  EncoderBuilder &set_option(std::string_view key, std::int64_t value);

  /// 校验 + 分配上下文 + 应用选项 + avcodec_open2。
  Result<Encoder> open();

  const EncoderConfig &config() const noexcept;

private:
  struct Option {
    std::string key;
    std::string value;
    bool is_integer = false;
    std::int64_t integer = 0;
  };

  EncoderConfig config_;
  std::vector<Option> options_;
};

/// 便捷入口：make_encoder(cfg) == EncoderBuilder{cfg}.open()
Result<Encoder> make_encoder(EncoderConfig config);

// ============================================================================
// BitstreamWriter —— 把包写成 Annex-B 裸流文件
//
//  把 "打开文件 / 写文件 / 统计 / 关文件" 也收进 RAII，调用方看不到 ofstream。
// ============================================================================

class BitstreamWriter {
public:
  static Result<BitstreamWriter> create(std::filesystem::path path);

  ~BitstreamWriter();
  BitstreamWriter(BitstreamWriter &&) noexcept;
  BitstreamWriter &operator=(BitstreamWriter &&) noexcept;
  BitstreamWriter(const BitstreamWriter &) = delete;
  BitstreamWriter &operator=(const BitstreamWriter &) = delete;

  Result<void> write(const Packet &packet);
  Result<void> write_all(std::span<const Packet> packets);

  std::uint64_t packet_count() const noexcept;
  Result<void> close();

private:
  struct Impl;
  explicit BitstreamWriter(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

} // namespace xencode
