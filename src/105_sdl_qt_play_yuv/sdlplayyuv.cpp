#include "sdlplayyuv.h"
#include "./ui_sdlplayyuv.h"
#include <QMessageBox>
#include <QTimer>
#include <SDL2/SDL.h>
#include <fstream>

static SDL_Window *sdl_win = nullptr;
static SDL_Renderer *sdl_renderer = nullptr;
static SDL_Texture *sdl_texture = nullptr;
static int sdl_width = 0;
static int sdl_height = 0;
static unsigned char *yuv = nullptr;
static std::ifstream yuv_file;

void SdlPlayYuv::timerEvent(QTimerEvent *ev) {
  // 防御性判断
  if (!yuv || !sdl_texture)
    return;

  // 帧大小
  int frame_size = sdl_width * sdl_height * 3 / 2;
  // 读取一帧数据
  yuv_file.read((char *)yuv, frame_size);

  // 如果文件读完了，重复文件指针（循环播放）
  if (yuv_file.gcount() < frame_size) {
    yuv_file.clear(); // 清除 EOF 标志
    yuv_file.seekg(0, std::ios::beg);
    yuv_file.read((char *)yuv, frame_size);
  }

  // 这里 pitch就是 Y一行大小，Y是亮度每一个像素一个所以就是 sdl_width的大小
  SDL_UpdateTexture(sdl_texture, NULL, yuv, sdl_width);
  SDL_RenderClear(sdl_renderer);
  SDL_Rect rect;
  rect.x = 0;
  rect.y = 0;
  rect.w = sdl_width;
  rect.h = sdl_height;
  SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, &rect);
  SDL_RenderPresent(sdl_renderer);
}

void SdlPlayYuv::initSDL() {
  // 首先打开 yuv 文件
  yuv_file.open("feiseyuexia.yuv", std::ios::binary); // 以二进制读取
  if (!yuv_file) {
    QMessageBox::information(this, "", "open yuv failed!");
    return;
  }
  // feiseyuexia.yuv 视频分辨率
  sdl_width = 480;
  sdl_height = 360;
  ui->label->resize(sdl_width, sdl_height);
  ui->label->move(0, 0);

  // 初始化 SDL
  SDL_Init(SDL_INIT_VIDEO);
  sdl_win = SDL_CreateWindowFrom((void *)ui->label->winId());
  if (!sdl_win) {
    qDebug() << "SDL_CreateWindowFrom failed: " << SDL_GetError();
    return;
  }
  sdl_renderer = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_ACCELERATED);
  if (!sdl_renderer) {
    qDebug() << "SDL_CreateRenderer failed: " << SDL_GetError();
    return;
  }
  // 创建支持 YUV 的材质
  sdl_texture =
      SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_IYUV,
                        SDL_TEXTUREACCESS_STREAMING, sdl_width, sdl_height);
  if (!sdl_texture) {
    qDebug() << "SDL_CreateTexture failed: " << SDL_GetError();
    return;
  }

  // 分配 要存储yuv的动态数组，大小计算
  // YUV420P
  // Y 平面：每个像素一个 Y -> WxH 字节个
  // U 平面：每 2x2 像素共用一个 U -> (W/2) x (H/2)字节个
  // V 平面：每 2x2 像素共用一个 V -> (W/2) x (H/2)字节个
  // 总数据 = Y + U + V = WxHx1.5 个字节
  // 每个像素平均 1.5字节
  int yuv_size = sdl_width * sdl_height * 3 / 2;
  yuv = new unsigned char[yuv_size];
  // 像素内每一位置0，全黑画面
  memset(yuv, 0, yuv_size);
  startTimer(33); // 大约 30 帧/秒，比 10ms(100fps)更接近播放视频效果
}

SdlPlayYuv::SdlPlayYuv(QWidget *parent)
    : QWidget(parent), ui(new Ui::SdlPlayYuv) {
  ui->setupUi(this);
  // 设置窗口大小
  resize(1024, 768);
  QTimer::singleShot(100, this, [this]() { initSDL(); });
}

SdlPlayYuv::~SdlPlayYuv() {
  if (yuv)
    delete[] yuv;
  if (sdl_texture)
    SDL_DestroyTexture(sdl_texture);
  if (sdl_renderer)
    SDL_DestroyRenderer(sdl_renderer);
  if (sdl_win)
    SDL_DestroyWindow(sdl_win);
  SDL_Quit();
  delete ui;
}
