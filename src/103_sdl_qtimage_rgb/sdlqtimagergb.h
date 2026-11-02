#ifndef SDLQTIMAGERGB_H
#define SDLQTIMAGERGB_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class SdlQtimageRgb;
}
QT_END_NAMESPACE

class SdlQtimageRgb : public QWidget
{
    Q_OBJECT

public:
    explicit SdlQtimageRgb(QWidget *parent = nullptr);
    ~SdlQtimageRgb() override;
    void timerEvent(QTimerEvent* ev) override;

private:
    Ui::SdlQtimageRgb *ui;
    QImage* qtImage = nullptr;
    int pix_size = 4;
};
#endif // SDLQTIMAGERGB_H
