#ifndef SDLQTMERGEPIC_H
#define SDLQTMERGEPIC_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class SdlQtMergePic;
}
QT_END_NAMESPACE

class SdlQtMergePic : public QWidget
{
    Q_OBJECT

public:
    explicit SdlQtMergePic(QWidget *parent = nullptr);
    ~SdlQtMergePic() override;
    void timerEvent(QTimerEvent* ev) override;

private:
    Ui::SdlQtMergePic *ui;
};
#endif // SDLQTMERGEPIC_H
