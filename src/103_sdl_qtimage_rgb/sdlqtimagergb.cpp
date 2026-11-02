#include "sdlqtimagergb.h"
#include "./ui_sdlqtimagergb.h"
#include <SDL2/SDL.h>
#include <QImage> // 使用 QImage 让 SDL只做计算
#include <QPixmap>
static int sdl_width = 0;
static int sdl_height = 0;
static unsigned char* rgb = nullptr;
void SdlQtimageRgb::timerEvent(QTimerEvent* ev)
{
    static int tmp = 255;
    tmp--;
    if (tmp < 0) tmp = 255; // 循环渐变

    // 1. 填充像素数据
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

    // 2. 拷贝给 QImage
    memcpy(qtImage->bits(), rgb, sdl_width * sdl_height * pix_size);

    // 3.直接丢给 QLabel 显示
    ui->label->setPixmap(QPixmap::fromImage(*qtImage));
}

SdlQtimageRgb::SdlQtimageRgb(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SdlQtimageRgb)
{
    ui->setupUi(this);
    // 计算尺寸
    qreal dpr = ui->label->devicePixelRatioF();
    sdl_width = ui->label->width();
    sdl_height = ui->label->height();

    int logical_w = sdl_width * dpr;
    int logical_h = sdl_height * dpr;

    ui->label->setFixedSize(logical_w, logical_h);
    ui->label->move(0,0);

    this->resize(logical_w, logical_h);
    if (sdl_width <= 0 || sdl_height <= 0) return; // 防御性检查


    // 分配内存和 QImage
    pix_size = 4;
    rgb = new unsigned char[sdl_width * sdl_height * pix_size];
    qtImage = new QImage(sdl_width, sdl_height, QImage::Format_ARGB32);

    startTimer(10);
}

SdlQtimageRgb::~SdlQtimageRgb()
{
    delete[] rgb;
    delete qtImage;
    delete ui;
}
