#include <fstream>
#include <iostream>
#include <vector>
extern "C" {
#include <libswscale/swscale.h>
}

const char *yuv_file = "setokai.yuv";
const char *rgba_file = "setokai.rgba";
const char *out_yuv_file = "setokai_reverse.yuv";

int main(int argc, char **argv) {
  std::cout << "111_test_sws_scale" << std::endl;
  // 1920 x 1080 YUV 转 1280 x 720 RGBA
  int yuv_width = 1920;
  int yuv_height = 1080;
  int rgba_width = 1280;
  int rgba_height = 720;

  // 平面存储YUV420P yyyy uu vv
  // 使用 std::vector<> 而不是 std::unique_ptr<> []{}
  // 分配 YUV 内存
  std::vector<unsigned char> yuv_y(yuv_width * yuv_height);
  std::vector<unsigned char> yuv_u(yuv_width * yuv_height / 4);
  std::vector<unsigned char> yuv_v(yuv_width * yuv_height / 4);
  // 分配 RGBA 内存
  std::vector<unsigned char> rgba(rgba_width * rgba_height * 4);

  // 构造 sws_scale 需要的裸指针数组和 linesize 数组
  uint8_t *src_data[3] = {yuv_y.data(), yuv_u.data(), yuv_v.data()};
  int src_linesize[3] = {yuv_width, yuv_width / 2, yuv_width / 2};

  uint8_t *dst_data[1] = {rgba.data()};
  int dst_linesize[1] = {rgba_width * 4};

  // std::unique_ptr<unsigned char> yuv[3]{};
  // int yuv_linesize[3] = {yuv_width, yuv_width / 2, yuv_width / 2};
  // yuv[0] = std::make_unique<unsigned char>(yuv_width * yuv_height);     // Y
  // yuv[1] = std::make_unique<unsigned char>(yuv_width * yuv_height / 4); // U
  // yuv[2] = std::make_unique<unsigned char>(yuv_width * yuv_height / 4); // V

  // 交错存储RGBA rgba rgba
  // std::unique_ptr<unsigned char> rgba =
  //     std::make_unique<unsigned char>(rgba_width * rgba_height * 4);
  // int rgba_linesize = rgba_width * 4; // 未对齐

  // 输入 yuv 文件
  std::ifstream ifs;
  ifs.open(yuv_file, std::ios::binary);
  if (!ifs) {
    std::cerr << "open yuv file " << yuv_file << " failed." << std::endl;
    return -1;
  }

  // 输出 rgba 文件
  std::ofstream ofs;
  ofs.open(rgba_file, std::ios::binary);
  if (!ofs) {
    std::cerr << "open rgba file " << rgba_file << " failed." << std::endl;
    return -1;
  }

  SwsContext *yuv2rgba = nullptr;
  int frame_count = 0;
  for (;;) {
    // 读取 YUV 帧
    ifs.read((char *)src_data[0], yuv_width * yuv_height);
    ifs.read((char *)src_data[1], yuv_width * yuv_height / 4);
    ifs.read((char *)src_data[2], yuv_width * yuv_height / 4);

    // 判断是否读取完毕
    // 如果读不到一整帧，退出循环
    if (ifs.gcount() < (yuv_width * yuv_height / 4))
      break;

    // YUV 转 RGBA
    // 转换上下文，NULL 新创建，非NULL判断与现有参数
    // 是否一致，一致直接返回，不一致先清理当前的
    // SwsContext 然后再创建
    yuv2rgba = sws_getCachedContext(
        yuv2rgba, yuv_width, yuv_height, // 输入宽高
        AV_PIX_FMT_YUV420P,              // 输入像素格式
        rgba_width, rgba_height,         // 输出宽高
        AV_PIX_FMT_RGBA,                 // 输出像素格式
        SWS_BILINEAR,                    // 选择支持变换的算法，双线性插值
        nullptr, nullptr, nullptr);      // 后三个参数（srcFilter, dstFilter,
                                    // param）通常传 NULL，FFmpeg 会使用默认值。

    if (!yuv2rgba) {
      std::cerr << "sws_getCachedContext failed." << std::endl;
      break;
    }

    int re = sws_scale(yuv2rgba,
                       src_data,      // 源数据裸指针数组
                       src_linesize,  // 源行字节数
                       0,             // 从第 0 行开始
                       yuv_height,    // 处理 height 行
                       dst_data,      // 目标数据裸指针数组
                       dst_linesize); // 目标行字节数
    // 写入文件（每帧写完）
    if (re > 0) {
      ofs.write((char *)dst_data[0], rgba_width * rgba_height * 4);
      std::cout << "Converting " << frame_count << " frames.\n";
      frame_count++;
    }
  }

  std::cout << "yuv2rgba Totally converted " << frame_count << " frames."
            << std::endl;

  ifs.close();
  ofs.close();

  frame_count = 0;
  ifs.open(rgba_file, std::ios::binary);
  if (!ifs) {
    std::cerr << "failed to open file " << rgba_file << std::endl;
    return -1;
  }

  ofs.open(out_yuv_file, std::ios::binary);
  if (!ofs) {
    std::cerr << "failed to open file " << out_yuv_file << std::endl;
    return -1;
  }

  SwsContext *rgba2yuv = nullptr;
  for (;;) {
    // 读取 RGBA 帧
    ifs.read((char *)dst_data[0], rgba_width * rgba_height * 4);

    if (ifs.gcount() < rgba_width * rgba_height * 4)
      break;

    rgba2yuv =
        sws_getCachedContext(rgba2yuv, rgba_width, rgba_height, AV_PIX_FMT_RGBA,
                             yuv_width, yuv_height, AV_PIX_FMT_YUV420P,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!rgba2yuv) {
      std::cerr << "sws_getCachedContext failed." << std::endl;
      return -1;
    }

    int re = sws_scale(rgba2yuv, dst_data, dst_linesize, 0, rgba_height,
                       src_data, src_linesize);
    if (re > 0) {
      ofs.write((char *)src_data[0], yuv_width * yuv_height);
      ofs.write((char *)src_data[1], yuv_width * yuv_height / 4);
      ofs.write((char *)src_data[2], yuv_width * yuv_height / 4);
      std::cout << "Converting " << frame_count << " frames.\n";
      frame_count++;
    }
  }
  std::cout << "rgba2yuv Totally converted " << frame_count << " frames."
            << std::endl;

  ofs.close();
  ifs.close();

  // 释放上下文
  sws_freeContext(yuv2rgba);
  sws_freeContext(rgba2yuv);
  return EXIT_SUCCESS;
}
