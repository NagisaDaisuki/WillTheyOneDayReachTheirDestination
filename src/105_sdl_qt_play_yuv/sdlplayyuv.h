#ifndef SDLPLAYYUV_H
#define SDLPLAYYUV_H

#include <QWidget>

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
  void initSDL();

private:
  Ui::SdlPlayYuv *ui;
};
#endif // SDLPLAYYUV_H
