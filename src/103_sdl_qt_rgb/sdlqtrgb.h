#ifndef SDLQTRGB_H
#define SDLQTRGB_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class SdlQtRgb;
}
QT_END_NAMESPACE

class SdlQtRgb : public QWidget
{
    Q_OBJECT

public:
    explicit SdlQtRgb(QWidget *parent = nullptr);
    ~SdlQtRgb() override;
    void timerEvent(QTimerEvent* ev) override;
private:
    Ui::SdlQtRgb *ui;
};
#endif // SDLQTRGB_H
