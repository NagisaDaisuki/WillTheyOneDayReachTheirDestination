#include "sdlqtrgb.h"
#include "./ui_sdlqtrgb.h"
#include <SDL2/SDL.h>

// #pragma comment(lib, "SDL2.lib")
// Win上引入库命令，Linux上直接
// -L/path/to/lib
// link_directories(/path/to/lib)

static int sdl_width = 0;
static int sdl_height = 0;
static int pix_size = 4;
static SDL_Window* sdl_window = nullptr;
static SDL_Renderer* sdl_render = nullptr;
static SDL_Texture* sdl_texture = nullptr;
static unsigned char* rgb = nullptr;

void SdlQtRgb::timerEvent(QTimerEvent* ev)
{
    static int tmp = 255;
    tmp--;
    for (int j = 0; j < sdl_height; ++j)
    {
        int b = j * sdl_width * pix_size;
        for (int i = 0; i < sdl_width * pix_size; i += pix_size)
        {
            rgb[b + i] = 0;
            rgb[b + i + 1] = tmp;
            rgb[b + i + 2] = 0;
            rgb[b + i + 3] = 255;
        }
    }

    SDL_UpdateTexture(sdl_texture, NULL, rgb, sdl_width * pix_size);
    SDL_RenderClear(sdl_render);
    SDL_Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = sdl_width;
    rect.h = sdl_height;

    SDL_RenderCopy(sdl_render, sdl_texture, NULL, &rect);
    SDL_RenderPresent(sdl_render);
}

SdlQtRgb::SdlQtRgb(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SdlQtRgb)
{
    ui->setupUi(this);
    // 获取真实的 Label 尺寸
    qreal dpr = ui->label->devicePixelRatioF();
    sdl_width = ui->label->width() * dpr;
    sdl_height = ui->label->height() * dpr;
    // 初始化
    SDL_Init(SDL_INIT_VIDEO);

    // 获取 QLabel 对应的句柄
    sdl_window = SDL_CreateWindowFrom((void*)ui->label->winId());
    // 创建渲染器
    sdl_render = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED);
    // 创建材质
    sdl_texture = SDL_CreateTexture(sdl_render,
                                    SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    sdl_width, sdl_height);
    rgb = new unsigned char[sdl_width * sdl_height * pix_size];
    startTimer(10);

}

SdlQtRgb::~SdlQtRgb()
{
    delete ui;
}
