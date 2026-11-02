#pragma once
#include "xvideoview.h"
struct SDL_Window; // 前向声明
struct SDL_Renderer;
struct SDL_Texture;

class XSDL : public XVideoView {
public:
  /////////////////////////////////////////
  //// 初始化渲染窗口 线程安全
  //// @para w 窗口宽度
  //// @para h 窗口高度
  //// @para fmt 像素格式
  //// @para win_id 窗口句柄(X11 Window), 如果为空，创建新窗口
  //// 重复调用会销毁旧的渲染器和纹理后重建，不泄漏资源
  //// @return 是否创建成功
  /////////////////////////////////////////
  bool Init(int w, int h, PixFormat fmt = RGBA,
            void *win_id = nullptr) override;

  /////////////////////////////////////////
  //// 渲染图像 线程安全
  //// @para data 渲染的二进制图像数据（YUV420P 时为 IYUV 连续内存）
  //// @para linesize 图像每行字节数, 对于 YUV420P就是Y一行字节数
  //// linesize <= 0 就根据宽度和像素格式自动算出大小
  //// @return 是否渲染成功
  /////////////////////////////////////////
  bool Draw(const unsigned char *data, int linesize = 0) override;

  /////////////////////////////////////////
  //// 三平面渲染重载 线程安全（FFmpeg AVFrame 兼容）
  //// @return 是否渲染成功
  /////////////////////////////////////////
  bool Draw(const unsigned char *y, int y_pitch, const unsigned char *u,
            int u_pitch, const unsigned char *v, int v_pitch) override;

  void Close() override;

  bool IsExit() override;

private:
  // UpdateTexture 之后清屏、缩放绘制并上屏（目标矩形由 RenderSetLogicalSize
  // 决定）
  bool Present();
// 计算默认 linesize；<=0 时按像素格式推算，失败返回 false
#ifdef FIX_LINE_SIZE
  bool FixLinesize(int *linesize);
#endif
  SDL_Window *win_ = nullptr;
  SDL_Renderer *render_ = nullptr;
  SDL_Texture *texture_ = nullptr;
};
