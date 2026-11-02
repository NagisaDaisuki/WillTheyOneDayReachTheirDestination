#ifndef SDLPLAYYUV_H
#define SDLPLAYYUV_H

#include <QWidget>
#include <fstream>
#include <memory>

#include "xvideoview.h"

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
  void timerEvent(QTimerEvent *ev) override;
  void resizeEvent(QResizeEvent *event) override;
  void initSDL();
  void ReverseRow(unsigned char *row, size_t n);

private:
  Ui::SdlPlayYuv *ui;
  // 播放状态全部成员化，不再使用静态全局变量
  int sdl_width_ = 0;
  int sdl_height_ = 0;
  std::unique_ptr<XVideoView> view_;
  std::unique_ptr<unsigned char[]> yuv_;
  std::ifstream yuv_file_;
};
#endif // SDLPLAYYUV_H
