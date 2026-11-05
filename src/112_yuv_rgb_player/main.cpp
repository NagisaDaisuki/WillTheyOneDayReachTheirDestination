#include "rgbyuvplayer.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    RgbYuvPlayer w;
    w.show();
    return QApplication::exec();
}
