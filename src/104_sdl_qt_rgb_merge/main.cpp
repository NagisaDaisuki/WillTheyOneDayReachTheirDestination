#include "sdlqtmergepic.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    SdlQtMergePic w;
    w.show();
    return QApplication::exec();
}
