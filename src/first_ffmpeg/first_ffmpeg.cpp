#include <iostream>
extern "C" {
#include <libavcodec/avcodec.h>
}
#pragma comment(lib, "avcodec.lib")

int main(int argc, char *argv[]) {
  std::cout << "first ffmpeg" << std::endl;
  std::cout << avcodec_configuration() << std::endl;
  return 0;
}
