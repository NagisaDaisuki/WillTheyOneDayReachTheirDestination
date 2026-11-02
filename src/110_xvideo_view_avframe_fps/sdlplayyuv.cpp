#include "sdlplayyuv.h"
#include "./ui_sdlplayyuv.h"
#include "xvideoview.h"
#include <QGuiApplication>
#include <QMessageBox>
#include <QTimer>
#include <SDL2/SDL.h>
#include <cstring>
#include <iostream>
#include <qnamespace.h>
#include <qpoint.h>
#include <qspinbox.h>
#include <sstream>
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

void SdlPlayYuv::moveEvent(QMoveEvent *event) {
  // 两个悬浮窗都是独立顶层窗口：move() 用全局屏幕坐标，
  // 必须跟随主窗口客户区原点，否则会被留在原地甚至跑到屏幕 (0,0)
  if (view_fps_)
    view_fps_->move(mapToGlobal(QPoint(4, 4)));
  if (set_fps_)
    set_fps_->move(mapToGlobal(QPoint(200, 4)));
  QWidget::moveEvent(event);
}

void SdlPlayYuv::View() {
  // 防御性编程
  if (!frame_)
    return;

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

  // 显示 XVideoView 实时计算的 fps 值
  std::stringstream ss;
  ss << "FPS: " << view_->render_fps();

  // 只能在槽函数中调用
  view_fps_->setText(ss.str().c_str());
  cur_fps_ = set_fps_->value(); // 拿到SpinBox设置的当前帧率
}

// 渲染线程函数
void SdlPlayYuv::Main() {
  while (!is_exit_) {
    ViewS();
    // 按 SpinBox 设置的帧率控制节奏；未设置时退回默认 40ms
    if (cur_fps_ > 0) {
      MSleep(1000 / cur_fps_);
    } else {
      MSleep(40);
    }
  }
}

void SdlPlayYuv::initSDL() {
  // 首先打开 yuv 文件
  yuv_file_.open("setokai.yuv", std::ios::binary); // 以二进制读取
  if (!yuv_file_) {
    QMessageBox::warning(this, "", "open yuv failed!");
    return;
  }

  // 绑定渲染函数信号槽
  connect(this, SIGNAL(ViewS()), this, SLOT(View()));

  // 显示 fps 控件
  view_fps_ = std::make_unique<QLabel>(nullptr);
  // 设置无边框且总在最前面
  view_fps_->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool |
                            Qt::WindowStaysOnTopHint);
  view_fps_->setAttribute(Qt::WA_TranslucentBackground);
  // 设置一个属性，让它能穿透鼠标点击
  view_fps_->setAttribute(Qt::WA_TransparentForMouseEvents);
  // 2. 设置一个足够大的固定大小，防止被挤压
  view_fps_->setFixedSize(100, 25);
  // 3. 设置 view_fps 样式
  view_fps_->setStyleSheet(
      "background-color: rgba(0,0,0,0); color: cyan; font-size: 16px;"
      "font-weight: bold;");
  // 4. 强制设置位置为 0,0
  // view_fps_->move(0, 0);
  // 父窗口 show() 之后才创建的子控件必须显式 show()，否则一直隐藏
  // （X11 下表现为：原生 X 窗口已创建但 map=no，屏幕上永远看不到）
  view_fps_->show();

  // 设置 fps 控件：与 FPS 悬浮窗同款（独立顶层无边框窗口）
  // 注意：SpinBox 需要接收鼠标点击调帧率，不能加 WA_TransparentForMouseEvents
  set_fps_ = std::make_unique<QSpinBox>(nullptr);
  set_fps_->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool |
                           Qt::WindowStaysOnTopHint);
  set_fps_->setValue(25);
  set_fps_->setRange(1, 300);
  set_fps_->show();

  // 注意：这里不能 raise()！ui->label 的原生 X 窗口要到下面 winId()
  // 才会被创建，而 X11 中新建窗口天然堆在兄弟之上，会反过来压住
  // 这两个控件。提升 z-order 必须等 SDL 窗口创建完之后再做

  // setokai.yuv 视频分辨率
  sdl_width_ = 1920;
  sdl_height_ = 1080;
  // ui->label->setGeometry(0, 0, width(), height());
  ui->label->resize(sdl_width_, sdl_height_);
  ui->label->move(0, 0);
  ui->label->setText("");

  // 构造一个 view对象
  view_.reset(XVideoView::Create());
  if (!view_) {
    QMessageBox::warning(this, "", "create view failed!");
    return;
  }

  if (!view_->Init(sdl_width_, sdl_height_, XVideoView::YUV420P,
                   (void *)ui->label->winId())) {
    QMessageBox::warning(this, "", "init view failed!");
    view_.reset();
    return;
  }

  // 两个悬浮窗都是独立顶层窗口，抬到最上层并定位到主窗口客户区
  // （顶层窗口 move 是全局屏幕坐标）：FPS 文本在 (4,4)，SpinBox 沿用
  // 原来的 x=200 放在其右侧
  set_fps_->raise();
  view_fps_->raise();
  view_fps_->move(mapToGlobal(QPoint(4, 4)));
  set_fps_->move(mapToGlobal(QPoint(200, 4)));

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

  // 主线程退出后，子线程不会自动退出
  // 在析构函数阻塞等待子线程释放
  th_ = std::thread(&SdlPlayYuv::Main, this);
}

SdlPlayYuv::SdlPlayYuv(QWidget *parent)
    : QWidget(parent), ui(new Ui::SdlPlayYuv) {
  ui->setupUi(this);
  // 设置窗口大小
  resize(1920, 1080);
  QTimer::singleShot(100, this, [this]() { initSDL(); });
}

SdlPlayYuv::~SdlPlayYuv() {
  // 处理 负责渲染的 线程函数
  is_exit_ = true;
  th_.join(); // 等待渲染线程退出
  // 1. 先停止定时器并销毁渲染后端（内部清理 GL/SDL 资源）
  view_.reset();
  // yuv_.reset();
  // 2. 关闭文件
  yuv_file_.close();
  // 3. 最后退出 SDL 全局状态
  SDL_Quit();
  delete ui;
}
