#include <iostream>
extern "C" {
#include <libavcodec/avcodec.h>
}
#pragma comment(lib, "avcodec.lib")

int main(int argc, char *argv[]) {
  // std::cout << avcodec_configuration() << std::endl;
  const AVCodec *h264 = avcodec_find_encoder_by_name("libx264");
  const AVCodec *h265 = avcodec_find_encoder_by_name("libx265");

  if (h264)
    printf("找到了 libx264 编码器！\n");
  else
    printf("没找到 libx264 编码器！\n");

  if (h265)
    printf("找到了 libx265 编码器！\n");
  else
    printf("没找到 libx265 编码器！\n");
  return 0;
}
