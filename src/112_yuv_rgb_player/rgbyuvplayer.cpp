#include "rgbyuvplayer.h"
#include "./ui_rgbyuvplayer.h"
#include "xvideoview.h"

#include <QFileDialog>
#include <QFontMetrics>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QTimer>
#include <chrono>
#include <iostream>

RgbYuvPlayer::RgbYuvPlayer(QWidget *parent)
    : QWidget(parent), ui(new Ui::RgbYuvPlayer) {
  ui->setupUi(this);
  QTimer::singleShot(100, this, [this]() { initSDL(); });
}

void RgbYuvPlayer::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  SyncScale();
}

void RgbYuvPlayer::moveEvent(QMoveEvent *event) { QWidget::moveEvent(event); }

void RgbYuvPlayer::SyncScale() {
  if (views_.size() < 2 || !ui->video1 || !ui->video2)
    return;
  // label 当前的真实大小交给渲染器，Present 里据此做等比缩放 + 居中
  views_[0]->Scale(ui->video1->width(), ui->video1->height());
  views_[1]->Scale(ui->video2->width(), ui->video2->height());
}

bool RgbYuvPlayer::eventFilter(QObject *watched, QEvent *event) {
  // label 尺寸变化（布局、父窗口 resize 等）时同步给渲染器
  if (event->type() == QEvent::Resize)
    SyncScale();
  return QWidget::eventFilter(watched, event);
}

void RgbYuvPlayer::AttachFpsOverlay(QLabel *fps, QWidget *video) {
  if (!fps || !video)
    return;

  // 关键点：video label 的 X11 窗口是被 SDL 直接绘制的（不经过 Qt 的绘制引擎），
  // 普通（非原生）子控件是画在父窗口的 backing store 上，会被 SDL 的画面盖住；
  // 而 WA_NativeWindow 会让 fps 控件拥有自己的 X 子窗口，X11 里子窗口永远
  // 叠在父窗口内容之上，所以能稳定贴在画面左上角
  fps->setParent(video);
  fps->setAttribute(Qt::WA_NativeWindow);
  fps->setAttribute(Qt::WA_TranslucentBackground); // ARGB 子窗口，半透明底
  fps->setAttribute(Qt::WA_TransparentForMouseEvents); // 不抢鼠标事件
  fps->setFrameShape(QFrame::NoFrame);
  fps->setStyleSheet("color: #00ff00; background-color: rgba(0, 0, 0, 150);"
                     "padding: 1px 5px; font-weight: bold;");
  fps->setText("FPS: 0");

  // 固定大小，避免文本变化时不断 adjustSize 导致抖动
  const QFontMetrics fm(fps->font());
  fps->setFixedSize(fm.horizontalAdvance("FPS: 888") + 14, fm.height() + 4);
  fps->move(8, 8);
  fps->show(); // 重新 setParent 后必须显式 show，否则一直隐藏
  fps->raise();
}

void RgbYuvPlayer::CheckFileSize(int i, int w, int h,
                                 XVideoView::PixFormat fmt) {
  const size_t file_size = views_[i]->file_size();
  const size_t frame_bytes = XVideoView::FrameBytes(w, h, fmt);
  if (file_size == 0 || frame_bytes == 0)
    return;

  const size_t frames = file_size / frame_bytes;
  std::cout << "file=" << file_size << " bytes, frame=" << frame_bytes
            << " bytes, frames=" << frames << std::endl;

  // 文件大小不是整数帧，说明宽/高/像素格式和文件实际内容对不上，
  // 继续播放会得到错位/斜切（看起来像“变形”）的画面
  if (file_size % frame_bytes != 0) {
    QMessageBox::warning(
        this, "尺寸可能填错了",
        QString("%1\n\n文件大小 %2 字节，不是 %3x%4（%5）单帧大小 %6 字节的整数倍。\n"
                "请确认输入的宽高和像素格式与文件一致，否则画面会错位/斜切。")
            .arg(i == 0 ? "video1" : "video2")
            .arg(file_size)
            .arg(w)
            .arg(h)
            .arg(ui->pix1->currentText())
            .arg(frame_bytes));
  }
}

void RgbYuvPlayer::ReverseRow(unsigned char *row, size_t n) {
  for (size_t i = 0, j = n - 1; i < n / 2; ++i, --j) {
    unsigned char tmp = row[i];
    row[i] = row[j];
    row[j] = tmp;
  }
}

void RgbYuvPlayer::Main() {
  while (!is_exit_) {
    ViewS();
    MSleep(10);
  }
}

void RgbYuvPlayer::View() {
  for (int i = 0; i < static_cast<int>(views_.size()); ++i) {
    // 每个 view 的 fps 上限由对应的 QSpinBox 决定
    QSpinBox *fps_box = (i == 0) ? ui->set_fps1 : ui->set_fps2;
    QLabel *fps_label = (i == 0) ? ui->fps1 : ui->fps2;
    const int fps = fps_box->value();
    if (fps <= 0)
      continue; // 防御性编程

    // 需要间隔的时间
    const int ms = 1000 / fps;

    // 判断是否到了可渲染时间
    if (NowTimePoint() - last_pts_[i] <= std::chrono::milliseconds(ms))
      continue;
    last_pts_[i] = NowTimePoint();
    auto frame = views_[i]->ReadFile();
    if (!frame)
      continue;

    views_[i]->DrawFrame(frame);

    // 显示 fps：只有数值真的变了才 setText（render_fps 一秒才更新一次，
    // 否则会以 25~30Hz 的频率重复重绘这个原生子窗口）
    const QString text = QString("FPS: %1").arg(views_[i]->render_fps());
    if (fps_label->text() != text)
      fps_label->setText(text);
  }
}

void RgbYuvPlayer::Open(int i) {
  if (i < 0 || i >= static_cast<int>(views_.size()))
    return;

  QFileDialog fd;
  auto filename = fd.getOpenFileName(); // 返回值是 QString
  if (filename.isEmpty())
    return; // 没有选择文件直接返回
  std::cout << filename.toLocal8Bit().data() << std::endl;
  // 调用初始化 view对应窗口的文件
  if (!views_[i]->OpenFile(filename.toLocal8Bit().toStdString()))
    return;

  int w{0};
  int h{0};
  QString pix{}; // YUV420P RGBA ARGB BGRA
  XVideoView::PixFormat pixfmt{XVideoView::YUV420P};

  if (i == 0) {
    w = ui->width1->value();
    h = ui->height1->value();
    pix = ui->pix1->currentText();
  } else if (i == 1) {
    w = ui->width2->value();
    h = ui->height2->value();
    pix = ui->pix2->currentText();
  }

  if (pix == "YUV420P")
    pixfmt = XVideoView::YUV420P;
  else if (pix == "RGBA")
    pixfmt = XVideoView::RGBA;
  else if (pix == "ARGB")
    pixfmt = XVideoView::ARGB;
  else if (pix == "BGRA")
    pixfmt = XVideoView::BGRA;
  else if (pix == "RGB24")
    pixfmt = XVideoView::RGB24;

  // 初始化窗口和材质
  if (!views_[i]->Init(w, h, pixfmt))
    std::cerr << "init view " << i << " failed" << std::endl;

  // 校验文件大小与宽高/格式是否匹配，对不上时提醒（否则画面会错位/斜切）
  CheckFileSize(i, w, h, pixfmt);

  // 尺寸变化窗口/材质后重新把 label 大小交给渲染器
  SyncScale();
}

void RgbYuvPlayer::Open1() { Open(0); }

void RgbYuvPlayer::Open2() { Open(1); }

void RgbYuvPlayer::initSDL() {

  // 绑定渲染函数信号槽
  connect(this, SIGNAL(ViewS()), this, SLOT(View()));
  views_.push_back(XVideoView::Create());
  views_.push_back(XVideoView::Create());
  views_[0]->set_win_id((void *)ui->video1->winId());
  views_[1]->set_win_id((void *)ui->video2->winId());
  last_pts_.assign(views_.size(), std::chrono::steady_clock::time_point{});

  // label 尺寸一变就同步给渲染器（固定布局下只在初始化时生效，
  // 换成 layout 后也能自动适配）
  ui->video1->installEventFilter(this);
  ui->video2->installEventFilter(this);
  SyncScale();

  // 把 fps 文本贴到两个 video label 的左上角（原生子窗口，不会被画面盖住）
  AttachFpsOverlay(ui->fps1, ui->video1);
  AttachFpsOverlay(ui->fps2, ui->video2);

  th_ = std::thread(&RgbYuvPlayer::Main, this);
}

RgbYuvPlayer::~RgbYuvPlayer() {
  is_exit_ = true;
  // 等待渲染线程退出
  if (th_.joinable())
    th_.join();

  // 释放渲染器资源（关闭 SDL 窗口/纹理/渲染器）
  for (auto *view : views_) {
    if (!view)
      continue;
    view->Close();
    delete view;
  }
  views_.clear();

  delete ui;
}
