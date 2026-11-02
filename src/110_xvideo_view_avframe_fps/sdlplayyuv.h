#ifndef SDLPLAYYUV_H
#define SDLPLAYYUV_H

#include "xvideoview.h"
#include <QLabel>
#include <QMoveEvent>
#include <QSpinBox>
#include <QWidget>
#include <fstream>
#include <memory>
#include <thread>

// 前向定义
struct AVFrame;

QT_BEGIN_NAMESPACE
namespace Ui {
class SdlPlayYuv;
}
QT_END_NAMESPACE

class SdlPlayYuv : public QWidget {
  Q_OBJECT

public:
  explicit SdlPlayYuv(QWidget *parent = nullptr);
  ~SdlPlayYuv() override;
  void resizeEvent(QResizeEvent *event) override;
  void moveEvent(QMoveEvent *event) override;
  void initSDL();
  void ReverseRow(unsigned char *row, size_t n);
  void Main(); // 线程入口函数
signals:
  void ViewS(); // 信号函数，将任务放在列表
public slots:
  void View(); // 槽函数，用来执行显示任务
private:
  Ui::SdlPlayYuv *ui;
  // 播放状态全部成员化，不再使用静态全局变量
  int sdl_width_ = 0;
  int sdl_height_ = 0;
  std::unique_ptr<XVideoView> view_;
  std::unique_ptr<AVFrame> frame_;
  std::ifstream yuv_file_;
  std::thread th_;                    // 线程变量
  bool is_exit_ = false;              // 处理线程退出
  std::unique_ptr<QLabel> view_fps_;  // 显示 fps 控件
  std::unique_ptr<QSpinBox> set_fps_; // 设置 fps 控件
  size_t file_size_;                  // 记录文件尾位置
  int cur_fps_ = 25;                  // 默认视频播放帧率
};
#endif // SDLPLAYYUV_H
