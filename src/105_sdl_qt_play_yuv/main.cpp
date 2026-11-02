#include "sdlplayyuv.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    SdlPlayYuv w;
    w.show();
    return QApplication::exec();
}
