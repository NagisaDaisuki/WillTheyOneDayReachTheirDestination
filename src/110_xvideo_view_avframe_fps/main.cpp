#include "sdlplayyuv.h"

#include <QApplication>

int main(int argc, char *argv[]) {
  QApplication a(argc, argv);
  SdlPlayYuv w;
  w.show(); // 这里第一次重载 resizeEvent()
  return QApplication::exec();
}
