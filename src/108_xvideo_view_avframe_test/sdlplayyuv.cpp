#include "sdlplayyuv.h"
#include "./ui_sdlplayyuv.h"
#include <QGuiApplication>
#include <QMessageBox>
#include <QTimer>
#include <SDL2/SDL.h>
#include <cstring>
#include <iostream>
extern "C" {
#include <libavcodec/avcodec.h>
}

void SdlPlayYuv::ReverseRow(unsigned char *row, size_t n) {
  for (size_t i = 0, j = n - 1; i < n / 2; ++i, --j) {
    unsigned char tmp = row[i];
    row[i] = row[j];
    row[j] = tmp;
  }
}

void SdlPlayYuv::resizeEvent(QResizeEvent *event) {
  // QWidget::resizeEvent(event);
  ui->label->resize(size());
  ui->label->move(0, 0);

  // 把实际窗口的物理像素大小同步给渲染端：
  // SDL 后端据此手动拉伸画面铺满整个嵌入窗口（无黑边）。
  // 必须判空：w.show() 会先于 initSDL 触发本函数，
  // 此时 view_ 尚未创建，直接访问会空指针崩溃（SIGSEGV）
  if (view_) {
    const qreal dpr = devicePixelRatioF();
    view_->Scale(width() * dpr, height() * dpr);
  }
}

void SdlPlayYuv::timerEvent(QTimerEvent *ev) {
  int y_size = sdl_width_ * sdl_height_;
  int uv_size = sdl_width_ * sdl_height_ / 4;
  int frame_size = y_size + uv_size * 2;

  // 用一个临时 buffer 读取整帧，然后按 linesize 逐行拷贝
  static unsigned char *temp_buf = new unsigned char[frame_size];

  yuv_file_.read((char *)temp_buf, frame_size);

  if (yuv_file_.gcount() < frame_size) {
    yuv_file_.clear();
    yuv_file_.seekg(0, std::ios::beg);
    yuv_file_.read((char *)temp_buf, frame_size);
  }

  // 1. 拷贝 Y 平面
  unsigned char *src = temp_buf;
  for (int i = 0; i < sdl_height_; ++i) {
    memcpy(frame_->data[0] + i * frame_->linesize[0], src + i * sdl_width_,
           sdl_width_);
  }
  src += y_size;

  // 2. 拷贝 U 平面
  for (int i = 0; i < sdl_height_ / 2; ++i) {
    memcpy(frame_->data[1] + i * frame_->linesize[1],
           src + i * (sdl_width_ / 2), sdl_width_ / 2);
  }
  src += uv_size;

  // 3. 拷贝 V 平面
  for (int i = 0; i < sdl_height_ / 2; ++i) {
    memcpy(frame_->data[2] + i * frame_->linesize[2],
           src + i * (sdl_width_ / 2), sdl_width_ / 2);
  }

  if (view_->IsExit()) {
    view_->Close();
    exit(0);
  }
  view_->DrawFrame(frame_.get());
}

// void SdlPlayYuv::timerEvent(QTimerEvent *ev) {
//   QWidget::timerEvent(ev);
//   // 防御性判断
//   if (!view_)
//     return;
//
//   // 帧大小
//   // const int frame_size = sdl_width_ * sdl_height_ * 3 / 2;
//   // 读取一帧数据
//   // yuv_file_.read((char *)yuv_.get(), frame_size);
//
//   // 读取 Y 平面
//   yuv_file_.read((char *)frame_->data[0], sdl_width_ * sdl_height_);
//   // 读取 U 平面
//   yuv_file_.read((char *)frame_->data[1], sdl_width_ * sdl_height_ / 4);
//   // 读取 V 平面
//   yuv_file_.read((char *)frame_->data[2], sdl_width_ * sdl_height_ / 4);
//
//   // 循环读取核心逻辑：如果读出来的字节不够一帧，说明到文件尾了
//   if (yuv_file_.gcount() < (sdl_width_ * sdl_height_ / 4)) {
//     yuv_file_.clear();                 // 清除 EOF 标志位
//     yuv_file_.seekg(0, std::ios::beg); // 游标回到文件头
//
//     // 重新读取第一帧
//     yuv_file_.read((char *)frame_->data[0], sdl_width_ * sdl_height_);
//     yuv_file_.read((char *)frame_->data[1], sdl_width_ * sdl_height_ / 4);
//     yuv_file_.read((char *)frame_->data[2], sdl_width_ * sdl_height_ / 4);
//   }
//
//   if (view_->IsExit()) {
//     view_->Close();
//     exit(0);
//   }
//   view_->DrawFrame(frame_.get());
//
//   // yuv镜像操作
//   // 考虑内存对齐
//   // yyyyyyyy uu vv
//   // y 平面镜像(planar)
//   // unsigned char *cur_yuv = nullptr;
//   // size_t y_size = (size_t)sdl_width_ * sdl_height_;
//   // size_t uv_size = y_size / 4;
//   // uint8_t *Y = yuv_.get();
//   // uint8_t *U = yuv_.get() + y_size;
//   // uint8_t *V = yuv_.get() + y_size + uv_size;
//
//   // // Y 平面镜像 (planar)
//   // for (int i = 0; i < sdl_height_; ++i) {
//   //   cur_yuv = Y + i * sdl_width_;
//   //   ReverseRow(cur_yuv, sdl_width_);
//   // }
//
//   // // U/V 平面镜像：色度平面行字节数是 宽/2（不是 高/2），
//   // // 之前误用 sdl_height_/2 作行步长导致行错位 -> 画面出彩色条纹
//   // for (int i = 0; i < sdl_height_ / 2; ++i) {
//   //   cur_yuv = U + i * (sdl_width_ / 2);
//   //   ReverseRow(cur_yuv, sdl_width_ / 2);
//   // }
//
//   // // V 平面镜像
//   // for (int i = 0; i < sdl_height_ / 2; ++i) {
//   //   cur_yuv = V + i * (sdl_width_ / 2);
//   //   ReverseRow(cur_yuv, sdl_width_ / 2);
//   // }
//
//   // // 如果文件读完了，重置文件指针（循环播放）
//   // if (yuv_file_.gcount() < frame_size) {
//   //   yuv_file_.clear(); // 清除 EOF 标志
//   //   yuv_file_.seekg(0, std::ios::beg);
//   //   // 重读完整一帧再画，避免画出残留的半帧旧数据导致闪烁
//   //   yuv_file_.read((char *)yuv_.get(), frame_size);
//   // }
//   // view_->Draw(yuv_.get(), sdl_width_);
// }

void SdlPlayYuv::initSDL() {
  // 首先打开 yuv 文件
  yuv_file_.open("feiseyuexia.yuv", std::ios::binary); // 以二进制读取
  if (!yuv_file_) {
    QMessageBox::warning(this, "", "open yuv failed!");
    return;
  }
  // 让 label 铺满整个 widget:
  // ui->label->setGeometry(0, 0, width(), height());

  // feiseyuexia.yuv 视频分辨率
  sdl_width_ = 480;
  sdl_height_ = 360;
  // ui->label->setGeometry(0, 0, width(), height());
  ui->label->resize(sdl_width_, sdl_height_);
  ui->label->move(0, 0);

  // 选择渲染后端：
  // Wayland 下 SDL_CreateWindowFrom 无法嵌入外部窗口（协议层不支持），
  // 自动切换到 QOpenGLWidget 渲染；X11 / Windows 下继续用 SDL 嵌入。
  XVideoView::RenderType type = XVideoView::SDL;
  if (QGuiApplication::platformName().contains("wayland")) {
    type = XVideoView::OPENGL;
  }

  // 构造一个 view对象
  view_.reset(XVideoView::Create(type));
  if (!view_) {
    QMessageBox::warning(this, "", "create view failed!");
    return;
  }

  // 传给后端的"窗口句柄"因后端而异：
  // SDL 后端要 X11 Window 句柄；OPENGL 后端要宿主 QWidget 指针
  void *win_id = (type == XVideoView::OPENGL) ? (void *)ui->label
                                              : (void *)ui->label->winId();
  // win_id = nullptr;
  if (!view_->Init(sdl_width_, sdl_height_, XVideoView::YUV420P, win_id)) {
    QMessageBox::warning(this, "", "init view failed!");
    view_.reset();
    return;
  }

  // 生成 AVFrame 对象空间
  frame_.reset(av_frame_alloc());
  frame_->width = sdl_width_;
  frame_->height = sdl_height_;
  frame_->format = AV_PIX_FMT_YUV420P;

  // 生成图像空间 默认 32 字节对齐（对于某些视频
  // 分辨率来说会出现
  // padding 添加的问题）
  // 用 手动赋值然后 在 DrawFrame
  // 时调用 frame 的 linesize
  // 避免出现对齐问题
  frame_->linesize[0] = sdl_width_;
  frame_->linesize[1] = sdl_width_ / 2;
  frame_->linesize[2] = sdl_width_ / 2;

  auto re = av_frame_get_buffer(frame_.get(), 0);
  if (re != 0) {
    char buf[1024] = {0};
    av_strerror(re, buf, sizeof(buf));
    std::cout << buf << std::endl;
  }

  // 分配 YUV 帧缓冲：Y + U + V = WxHx1.5 字节
  // const int yuv_size = sdl_width_ * sdl_height_ * 3 / 2;
  // yuv_.reset(new unsigned char[yuv_size]);
  // memset(yuv_.get(), 0, yuv_size);

  startTimer(33); // 大约 30 帧/秒
}

SdlPlayYuv::SdlPlayYuv(QWidget *parent)
    : QWidget(parent), ui(new Ui::SdlPlayYuv) {
  ui->setupUi(this);
  // 设置窗口大小
  resize(1024, 768);
  QTimer::singleShot(100, this, [this]() { initSDL(); });
}

SdlPlayYuv::~SdlPlayYuv() {
  // 1. 先停止定时器并销毁渲染后端（内部清理 GL/SDL 资源）
  view_.reset();
  // yuv_.reset();
  // 2. 关闭文件
  yuv_file_.close();
  // 3. 最后退出 SDL 全局状态
  SDL_Quit();
  delete ui;
}
