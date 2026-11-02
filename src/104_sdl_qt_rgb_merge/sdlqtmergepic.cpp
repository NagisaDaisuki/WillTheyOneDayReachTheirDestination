#include "sdlqtmergepic.h"
#include "./ui_sdlqtmergepic.h"
#include <SDL2/SDL.h>
#include <QMessageBox>
#include <cstring>
static SDL_Window* sdl_win = nullptr;
static SDL_Renderer* sdl_renderer = nullptr;
static SDL_Texture* sdl_texture = nullptr;
static int sdl_width = 0;
static int sdl_height = 0;
static unsigned char* rgb;
static size_t pix_size = 4;

void SdlQtMergePic::timerEvent(QTimerEvent* ev)
{
    if (!rgb || !sdl_texture) return;

    SDL_UpdateTexture(sdl_texture, NULL, rgb, sdl_width * pix_size);
    SDL_RenderClear(sdl_renderer);
    SDL_Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = sdl_width;
    rect.h = sdl_height;

    SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, &rect);
    SDL_RenderPresent(sdl_renderer);
}

SdlQtMergePic::SdlQtMergePic(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SdlQtMergePic)
{
    ui->setupUi(this);
    // SDL 复用 Qt 已经建立好的 Wayland 连接
    // 在 Qt 中获取 wl_display 的指针
    // auto wl_display_ptr = QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("display");
    // SDL_SetHintWithPriority(
    //     // SDL_HINT_VIDEO_WAYLAND_WL_DISPLAY,
    //     SDL_SetPointerProperty,
    //     wl_display_ptr,
    //     SDL_HINT_OVERRIDE);

    // 加载图片
    QImage img1("001.jpg");
    QImage img2("002.jpg");
    if (img1.isNull() || img2.isNull()){
        QMessageBox::information(this, "", "open image failed!");
        return;
    }
    img1 = img1.convertToFormat(QImage::Format_ARGB32);
    img2 = img2.convertToFormat(QImage::Format_ARGB32);

    qreal dpr = devicePixelRatioF();

    // 缩放大小
    sdl_width = img1.width() + img2.width();
    sdl_height = qMax(img1.height(), img2.height());

    // 实际大小
    int logical_w = sdl_width / dpr;
    int logical_h = sdl_height / dpr;

    ui->label->setFixedSize(logical_w, logical_h);
    ui->label->move(0,0);

    // 调整主窗口大小
    this->resize(logical_w, logical_h);

    // 初始化 SDL 和纹理
    SDL_Init(SDL_INIT_VIDEO);
    sdl_win = SDL_CreateWindowFrom((void*)ui->label->winId());
    // 使用软件渲染或硬件加速
    sdl_renderer = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_ACCELERATED);

    sdl_texture = SDL_CreateTexture(sdl_renderer,
                                   SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   sdl_width, sdl_height);
    // 设置混合纹理模式，防止 Alpha 为 0 时什么都看不见
    SDL_SetTextureBlendMode(sdl_texture, SDL_BLENDMODE_NONE);

    // 分配内存并初始化
    rgb = new unsigned char [sdl_width * sdl_height * pix_size];
    memset(rgb, 0, sdl_width * sdl_height * pix_size);

    for (int i = 0; i < sdl_width * sdl_height; ++i)
    {
        rgb[i * pix_size + 3] = 0;
    }

    for (int i = 0; i < sdl_height; ++i){
        int b = i * sdl_width * pix_size;
        if (i < img1.height())
            memcpy(rgb + b, img1.scanLine(i), img1.width() * pix_size);
        b += img1.width() * pix_size;
        if (i < img2.height())
            memcpy(rgb + b, img2.scanLine(i), img2.width() * pix_size);
    }
    QImage out(rgb, sdl_width, sdl_height, QImage::Format_ARGB32);
    out.save("out.png");
    startTimer(10);

}

SdlQtMergePic::~SdlQtMergePic()
{
    delete ui;
    // if (rgb) delete[] rgb;
}
