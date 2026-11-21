#include "xvideoview.h"
#include "xsdl.h"
extern "C" {
#include <libavcodec/avcodec.h>
}
#include <iostream>
#include <thread>

void MSleep(unsigned long ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

namespace {

// linesize 通常因 CPU 对齐大于图像真实宽度（av_frame_get_buffer 默认按
// av_cpu_max_align 对齐）。若直接 read(linesize * height)，对齐填充字节会被
// 当成像素读走，导致后面每一帧整体错位，所以必须逐行只读真实宽度的字节。
bool ReadPlane(std::ifstream &ifs, unsigned char *dst, int pitch, int width,
               int height) {
  if (!dst || pitch <= 0 || width <= 0 || height <= 0 || width > pitch)
    return false;

  if (pitch == width) { // 无填充，一次读完
    const auto total = static_cast<std::streamsize>(width) * height;
    ifs.read(reinterpret_cast<char *>(dst), total);
    return ifs.gcount() == total;
  }

  for (int y = 0; y < height; ++y) {
    ifs.read(reinterpret_cast<char *>(dst + static_cast<size_t>(y) * pitch),
             width);
    if (ifs.gcount() != width)
      return false;
  }
  return true;
}

// RGB 类像素格式每像素字节数，未知格式返回 0
int BytesPerPixel(int fmt) {
  switch (fmt) {
  case AV_PIX_FMT_RGB24:
    return 3;
  case AV_PIX_FMT_RGBA:
  case AV_PIX_FMT_ARGB:
  case AV_PIX_FMT_BGRA:
    return 4;
  default:
    return 0;
  }
}

} // namespace

std::chrono::steady_clock::time_point NowTimePoint() {
  return std::chrono::steady_clock::now();
}

bool XVideoView::OpenFile(std::string filepath) {
  if (ifs_.is_open())
    ifs_.close();
  file_size_ = 0;
  ifs_.open(filepath, std::ios::binary);
  if (!ifs_.is_open())
    return false;

  // 记录文件总字节数：上层据此校验宽高/像素格式是否填对
  ifs_.seekg(0, std::ios::end);
  file_size_ = static_cast<size_t>(ifs_.tellg());
  ifs_.seekg(0, std::ios::beg);
  return true;
}

size_t XVideoView::FrameBytes(int w, int h, PixFormat fmt) {
  if (w <= 0 || h <= 0)
    return 0;
  switch (fmt) {
  case YUV420P:
    return static_cast<size_t>(w) * h * 3 / 2; // Y + 半个分辨率的 U/V
  case RGB24:
    return static_cast<size_t>(w) * h * 3;
  default:
    return static_cast<size_t>(w) * h * 4; // RGBA/ARGB/BGRA
  }
}

AVFrame *XVideoView::ReadFile() {
  if (width_ <= 0 || height_ <= 0 || !ifs_)
    return nullptr;

  // AVFrame 空间已经申请，如果参数发生变化，需要释放空间
  if (frame_) {
    if (frame_->width != width_ || frame_->height != height_ ||
        frame_->format != fmt_) {
      // 释放 AVFrame 对象空间
      // buf 引用技术减一
      av_frame_free(&frame_);
    }
  }

  if (!frame_) {
    frame_ = av_frame_alloc();
    if (!frame_)
      return nullptr;
    // 分配对象空间和像素空间
    frame_->width = width_;
    frame_->height = height_;
    frame_->format = fmt_;

    // 生成 AVFrame 空间。align 传 1 而不是 0：让 linesize 紧排（= 宽*字节数），
    // 一是文件可以一整帧一次读完（不用逐行），
    // 二是交给 SDL 的 pitch 不带填充——SDL 的 GL 后端会把 pitch 换算成
    // GL_UNPACK_ROW_LENGTH = pitch/字节数（整数除法），RGB24 这类 3 字节格式
    // 一旦 pitch%3 != 0 就会逐行错位成斜切
    auto re = av_frame_get_buffer(frame_, 0);
    if (re != 0) {
      char buf[1024] = {0};
      av_strerror(re, buf, sizeof(buf) - 1);
      std::cout << "av_frame_get_buffer failed: " << buf << std::endl;
      av_frame_free(&frame_);
    }
  }

  if (!frame_)
    return nullptr;

  // 读取一帧数据（linesize 紧排，通常整帧一次读完；ReadPlane 兼容带填充的情况）
  bool ok = false;
  if (frame_->format == AV_PIX_FMT_YUV420P) {
    ok = ReadPlane(ifs_, frame_->data[0], frame_->linesize[0], width_,
                   height_) &&
         ReadPlane(ifs_, frame_->data[1], frame_->linesize[1], width_ / 2,
                   height_ / 2) &&
         ReadPlane(ifs_, frame_->data[2], frame_->linesize[2], width_ / 2,
                   height_ / 2);
  } else {
    const int bpp = BytesPerPixel(frame_->format);
    if (bpp <= 0) {
      std::cout << "unsupported pixel format: " << frame_->format << std::endl;
      return nullptr;
    }
    ok = ReadPlane(ifs_, frame_->data[0], frame_->linesize[0], width_ * bpp,
                   height_);
  }

  // 文件结束或最后一帧数据不完整
  if (!ok)
    return nullptr;

  return frame_;
}

XVideoView *XVideoView::Create(RenderType type) {
  switch (type) {
  case XVideoView::SDL:
    return new XSDL();
  case XVideoView::OPENGL:
    // return new XOpenGL();
  default:
    break;
  }

  return nullptr;
}

void XVideoView::Scale(int w, int h) {
  // Scale 可能由 UI 线程调用，而渲染线程正在 Present 里读它，需要加锁
  std::unique_lock<std::mutex> lock(mtx_);
  scale_w_ = w;
  scale_h_ = h;
}

bool XVideoView::DrawFrame(AVFrame *frame) {
  if (!frame || !frame->data[0])
    return false;
  // 计算一秒显示帧率
  count_++;
  // 获取当前时间点
  auto now = std::chrono::steady_clock::now();

  // 如果 beg_time_ 是默认值（刚初始化，还没记录过）
  // 可以用 time_since_epoch().count == 0 来判断
  if (beg_time_.time_since_epoch().count() == 0) {
    beg_time_ = now;
  }
  // 如果距离上次记录，已经过去了 >= 1秒
  else if (now - beg_time_ >= std::chrono::seconds(1)) {
    render_fps_ = count_;
    count_ = 0;
    beg_time_ = now;
  }

  switch (frame->format) {
  case AV_PIX_FMT_YUV420P:
    return Draw(frame->data[0], frame->linesize[0], frame->data[1],
                frame->linesize[1], frame->data[2], frame->linesize[2]);
  case AV_PIX_FMT_RGBA:
  case AV_PIX_FMT_ARGB:
  case AV_PIX_FMT_BGRA:
  case AV_PIX_FMT_RGB24:
    // 单平面打包格式
    return Draw(frame->data[0], frame->linesize[0]);
  default:
    std::cerr << "XVideoView: unsupported pixel format " << frame->format
              << std::endl;
    return false;
  }
}
