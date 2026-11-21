// ============================================================================
//  encoder.cpp —— EncoderBuilder / Encoder 的实现
// ============================================================================

#include "detail/ffi.h"

#include <iterator>

namespace xencode {

using detail::make_error;

namespace {

// ---------------------------------------------------------------------------
// 编码器策略表
//
//  把"不同编码器的差异"收敛到一张表里，而不是散落在调用方的 switch 里。
//  新增 AV1/VP9 只需要在这张表加一行 + Codec 枚举加一项，
//  Encoder / EncoderBuilder / 调用方代码一行都不用改。
// ---------------------------------------------------------------------------

struct CodecPolicy {
  AVCodecID id;
  const char *label;
  /// 质量档位对应的 AVOption 名（x264/x265 都是 "crf"）。
  const char *quality_option;
};

constexpr CodecPolicy kPolicies[] = {
    {AV_CODEC_ID_H264, "h264", "crf"},
    {AV_CODEC_ID_HEVC, "hevc", "crf"},
};

const CodecPolicy *policy_for(Codec codec) noexcept {
  for (const auto &policy : kPolicies) {
    if (to_string(codec) == std::string_view{policy.label})
      return &policy;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// 选项设置小工具：统一走 AV_OPT_SEARCH_CHILDREN
//
// 原版直接写 av_opt_set(c_->priv_data, key, ...)，只能命中"编码器私有"选项，
// 一旦用户想设的是 AVCodecContext 自己的通用选项（例如 "threads"、"preset"
// 之外的 ctx 级选项）就会失败。加 AV_OPT_SEARCH_CHILDREN 后两处都能命中。
// ---------------------------------------------------------------------------

Result<void> set_option_str(AVCodecContext *ctx, const char *key,
                            const char *value) {
  const int ret = av_opt_set(ctx, key, value, AV_OPT_SEARCH_CHILDREN);
  if (ret < 0)
    return std::unexpected(make_error(ErrorCode::option_rejected, ret, key));
  return {};
}

Result<void> set_option_int(AVCodecContext *ctx, const char *key,
                            std::int64_t value) {
  const int ret = av_opt_set_int(ctx, key, value, AV_OPT_SEARCH_CHILDREN);
  if (ret < 0)
    return std::unexpected(make_error(ErrorCode::option_rejected, ret, key));
  return {};
}

// ---------------------------------------------------------------------------
// 码率控制：把声明式的 RateControl 翻译成 FFmpeg 的字段组合
// ---------------------------------------------------------------------------

Result<void> apply_rate_control(const EncoderConfig &config,
                                const CodecPolicy &policy,
                                AVCodecContext *ctx) {
  const auto bitrate = static_cast<std::int64_t>(config.bitrate_kbps) * 1000;
  const auto buffer = static_cast<std::int64_t>(config.buffer_kbps) * 1000;

  switch (config.rate_control) {
  case RateControl::constant_quality:
    // 纯 CRF：只管画质，体积完全交给内容决定。
    return set_option_int(ctx, policy.quality_option, config.constant_quality);

  case RateControl::constant_quality_capped:
    // CRF + VBV 上限：保画质的同时给码率上封顶（B站/YouTube 推荐）。
    if (auto applied =
            set_option_int(ctx, policy.quality_option, config.constant_quality);
        !applied)
      return applied;
    ctx->rc_max_rate = static_cast<int>(bitrate);
    ctx->rc_buffer_size = static_cast<int>(buffer);
    return {};

  case RateControl::constant_bitrate:
    // CBR：目标=最小=最大，并开 nal-hrd 填充，保证每秒真的吐出这么多字节。
    ctx->bit_rate = static_cast<int>(bitrate);
    ctx->rc_min_rate = static_cast<int>(bitrate);
    ctx->rc_max_rate = static_cast<int>(bitrate);
    ctx->rc_buffer_size = static_cast<int>(buffer);
    ctx->rc_initial_buffer_occupancy = ctx->rc_buffer_size * 3 / 4;
    return set_option_str(ctx, "nal-hrd", "cbr");

  case RateControl::average_bitrate:
    ctx->bit_rate = static_cast<int>(bitrate);
    ctx->rc_max_rate = static_cast<int>(bitrate);
    ctx->rc_buffer_size = static_cast<int>(buffer);
    return {};
  }

  return std::unexpected(
      make_error(ErrorCode::invalid_config, 0, "unknown rate control mode"));
}

// ---------------------------------------------------------------------------
// 收包：把编码器输出队列里已有的包全部取出来
//
//  返回空 vector 是**正常情况**（编码器还在缓冲），不是失败。
//  只有 AVERROR(EAGAIN) / AVERROR_EOF 表示"取完了"，其它负值才是错误。
// ---------------------------------------------------------------------------

Result<std::vector<Packet>> drain_packets(AVCodecContext *ctx) {
  std::vector<Packet> packets;

  for (;;) {
    // 先把盒子准备好；失败路径由 unique_ptr 兜住，不会泄漏。
    detail::PacketPtr guard(av_packet_alloc());
    if (!guard)
      return std::unexpected(
          make_error(ErrorCode::frame_alloc_failed, AVERROR(ENOMEM),
                     "av_packet_alloc failed"));

    const int ret = avcodec_receive_packet(ctx, guard.get());
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break; // 取完了
    if (ret < 0)
      return std::unexpected(make_error(ErrorCode::receive_failed, ret));

    packets.push_back(detail::FfiAccess::adopt(guard.release()));
  }
  return packets;
}

void append_packets(std::vector<Packet> &dst, std::vector<Packet> &&src) {
  dst.insert(dst.end(), std::make_move_iterator(src.begin()),
             std::make_move_iterator(src.end()));
}

/// 防御性上限：正常路径下每次 send→EAGAIN 都必然能收到至少一个包，
/// 因此这个循环最多转一圈。带上限只是为了在异常实现下不死循环。
constexpr int kMaxRetries = 64;

} // namespace

// ---------------------------------------------------------------------------
// EncoderBuilder
// ---------------------------------------------------------------------------

EncoderBuilder::EncoderBuilder(EncoderConfig config) : config_(config) {}

EncoderBuilder &EncoderBuilder::set_option(std::string_view key,
                                           std::string_view value) {
  options_.push_back(Option{std::string{key}, std::string{value}, false, 0});
  return *this;
}

EncoderBuilder &EncoderBuilder::set_option(std::string_view key,
                                           std::int64_t value) {
  options_.push_back(Option{std::string{key}, {}, true, value});
  return *this;
}

const EncoderConfig &EncoderBuilder::config() const noexcept { return config_; }

Result<Encoder> EncoderBuilder::open() {
  // 1. 参数校验（原版完全没有，尺寸忘设也能"打开成功"，直到 CreateFrame 才炸）
  if (auto valid = config_.validate(); !valid)
    return std::unexpected(valid.error());

  // 2. 找编码器
  const CodecPolicy *policy = policy_for(config_.codec);
  if (policy == nullptr)
    return std::unexpected(make_error(ErrorCode::codec_not_found, 0,
                                      "no policy for requested codec"));

  const AVCodec *codec = avcodec_find_encoder(policy->id);
  if (codec == nullptr)
    return std::unexpected(make_error(ErrorCode::codec_not_found,
                                      AVERROR_ENCODER_NOT_FOUND,
                                      std::string{policy->label}));

  // 3. 分配上下文（unique_ptr 保证后面任何一步失败都不泄漏）
  detail::CtxPtr ctx(avcodec_alloc_context3(codec));
  if (!ctx)
    return std::unexpected(make_error(ErrorCode::context_alloc_failed,
                                      AVERROR(ENOMEM)));

  ctx->time_base = AVRational{1, config_.fps};
  ctx->framerate = AVRational{config_.fps, 1};
  ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  ctx->width = config_.width;
  ctx->height = config_.height;
  ctx->thread_count = config_.thread_count; // 0 = 让 FFmpeg 自己决定

  // 4. 码率控制
  if (auto applied = apply_rate_control(config_, *policy, ctx.get()); !applied)
    return std::unexpected(applied.error());

  // 5. 用户选项（在 open 之前应用，这个顺序由类型系统保证：
  //    set_option 只存在于 EncoderBuilder 上，Encoder 上没有）
  for (const auto &option : options_) {
    auto applied =
        option.is_integer
            ? set_option_int(ctx.get(), option.key.c_str(), option.integer)
            : set_option_str(ctx.get(), option.key.c_str(),
                             option.value.c_str());
    if (!applied)
      return std::unexpected(applied.error());
  }

  // 6. 打开
  if (const int ret = avcodec_open2(ctx.get(), codec, nullptr); ret < 0)
    return std::unexpected(make_error(ErrorCode::open_failed, ret));

  return Encoder(
      std::make_unique<detail::EncoderImpl>(std::move(ctx), config_));
}

// ---------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------

Encoder::Encoder(std::unique_ptr<detail::EncoderImpl> impl) noexcept
    : impl_(std::move(impl)) {}

Encoder::~Encoder() = default;
Encoder::Encoder(Encoder &&) noexcept = default;
Encoder &Encoder::operator=(Encoder &&) noexcept = default;

const EncoderConfig &Encoder::config() const noexcept {
  return impl_->config;
}

Result<Frame> Encoder::create_frame() const {
  if (!impl_ || !impl_->ctx)
    return std::unexpected(make_error(ErrorCode::invalid_frame, 0,
                                      "encoder is not open"));

  // 先交给 RAII 保管，任何一条失败分支都会自动 av_frame_free。
  detail::FramePtr guard(av_frame_alloc());
  if (!guard)
    return std::unexpected(make_error(ErrorCode::frame_alloc_failed,
                                      AVERROR(ENOMEM)));

  guard->width = impl_->ctx->width;
  guard->height = impl_->ctx->height;
  guard->format = impl_->ctx->pix_fmt;
  guard->color_range = impl_->ctx->color_range;

  if (const int ret = av_frame_get_buffer(guard.get(), 0); ret < 0)
    return std::unexpected(make_error(ErrorCode::frame_alloc_failed, ret));

  return detail::FfiAccess::adopt(guard.release());
}

Result<std::vector<Packet>> Encoder::encode(const Frame &frame) {
  if (!impl_ || !impl_->ctx)
    return std::unexpected(make_error(ErrorCode::invalid_frame, 0,
                                      "encoder is not open"));

  // 状态机：flush 之后不再接受新帧。
  // 原版没有这个状态，flush 之后继续 send 是 UB，只能靠 AVERROR_EOF 兜着。
  if (impl_->flushed)
    return std::unexpected(make_error(
        ErrorCode::already_flushed, 0, "encoder has already been flushed"));

  const AVFrame *raw_frame = detail::FfiAccess::raw(frame);
  if (raw_frame == nullptr)
    return std::unexpected(make_error(ErrorCode::invalid_frame, 0,
                                      "frame is empty or moved-from"));

  AVCodecContext *ctx = impl_->ctx.get();
  std::vector<Packet> output;

  int ret = avcodec_send_frame(ctx, raw_frame);

  // ---------------------------------------------------------------------
  // 【关键修复】AVERROR(EAGAIN) 的语义是"输入队列满，本帧未被接收"。
  // 正确做法：先把输出队列抽干腾出空间，再重试 send，直到本帧被收下。
  // 原版把 EAGAIN 当成无所谓直接返回，等于静默丢掉这一帧。
  // ---------------------------------------------------------------------
  for (int attempt = 0; ret == AVERROR(EAGAIN); ++attempt) {
    if (attempt >= kMaxRetries)
      return std::unexpected(make_error(
          ErrorCode::send_failed, ret,
          "encoder refused frame repeatedly after draining"));

    auto drained = drain_packets(ctx);
    if (!drained)
      return std::unexpected(drained.error());
    append_packets(output, std::move(*drained));

    ret = avcodec_send_frame(ctx, raw_frame);
  }

  if (ret < 0)
    return std::unexpected(make_error(ErrorCode::send_failed, ret));

  auto drained = drain_packets(ctx);
  if (!drained)
    return std::unexpected(drained.error());
  append_packets(output, std::move(*drained));

  // 可能是空 vector —— 这是正常情况（B 帧/前瞻缓冲），不是失败。
  return output;
}

Result<std::vector<Packet>> Encoder::flush() {
  if (!impl_ || !impl_->ctx)
    return std::unexpected(
        make_error(ErrorCode::invalid_frame, 0, "encoder is not open"));

  if (impl_->flushed)
    return std::unexpected(make_error(ErrorCode::already_flushed, 0,
                                      "flush() may only be called once"));

  AVCodecContext *ctx = impl_->ctx.get();
  std::vector<Packet> output;

  int ret = avcodec_send_frame(ctx, nullptr); // NULL = 进入冲刷模式

  for (int attempt = 0; ret == AVERROR(EAGAIN); ++attempt) {
    if (attempt >= kMaxRetries)
      return std::unexpected(make_error(
          ErrorCode::flush_failed, ret, "flush stuck in EAGAIN"));

    auto drained = drain_packets(ctx);
    if (!drained)
      return std::unexpected(drained.error());
    append_packets(output, std::move(*drained));

    ret = avcodec_send_frame(ctx, nullptr);
  }

  // 已经冲刷过再 send NULL 会得到 AVERROR_EOF，这是正常终止信号。
  if (ret < 0 && ret != AVERROR_EOF)
    return std::unexpected(make_error(ErrorCode::flush_failed, ret));

  impl_->flushed = true;

  auto drained = drain_packets(ctx);
  if (!drained)
    return std::unexpected(drained.error());
  append_packets(output, std::move(*drained));

  return output;
}

// ---------------------------------------------------------------------------
// 便捷入口
// ---------------------------------------------------------------------------

Result<Encoder> make_encoder(EncoderConfig config) {
  return EncoderBuilder{config}.open();
}

} // namespace xencode
