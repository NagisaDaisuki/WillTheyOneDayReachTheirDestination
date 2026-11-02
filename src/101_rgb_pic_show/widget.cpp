#include "widget.h"
#include "./ui_widget.h"
#include <QPainter>
#include <QImage>

static int w = 1024;
static int h = 768;

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);
    resize(1024, 768);
}

void Widget::paintEvent(QPaintEvent* ev)
{
    // 假设 w 和 h 是成员变量或局部计算出来的尺寸
    QImage img(w, h, QImage::Format_RGB888);
    auto d = img.bits(); // d 是 uchar*
    unsigned char r = 255;

    // 获取真实的行字节数(处理对齐填充)
    /*
     * Qt 在分配图像内存时，为了保证绘制效率，每行数据（Stride）的字节数会被填充到 4 的倍数。
     */
    int bytesPerLine = img.bytesPerLine();

    for (int i = 0; i < h; ++i)
    {
        r--;
        // 用 bytesPerLine 代替 w * 3
        int b = i * bytesPerLine;

        for (int j = 0; j < w * 3; j += 3)
        {
            // 这里的 j 依然 按照 3 的倍数
            // 递增，因为每个像素确实占 3 个字节
            //  但 b 的偏移量必须基于真实的一行字节数
            d[b + j] = r; 	// R
            d[b + j + 1] = 0;	// G
            d[b + j + 2] = 0;	// B
        }
    }

    QPainter p(this);
    p.begin(this);
    p.drawImage(0,0,img);
    p.end();
}

Widget::~Widget()
{
    delete ui;
}
