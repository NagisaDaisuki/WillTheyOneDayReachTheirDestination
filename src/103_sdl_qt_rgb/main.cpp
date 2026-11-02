#include "sdlqtrgb.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    SdlQtRgb w;
    w.show();
    return QApplication::exec();
}
