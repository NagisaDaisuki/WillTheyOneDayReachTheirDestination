#include "sdlqtimagergb.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    SdlQtimageRgb w;
    w.show();
    return QApplication::exec();
}
