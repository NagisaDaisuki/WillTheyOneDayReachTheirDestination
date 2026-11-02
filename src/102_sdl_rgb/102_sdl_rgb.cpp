#include <SDL2/SDL.h>
#include <iostream>
#include <memory>
#undef main

// MSVC 引入 lib，gcc -L 或 CMake link_libraries
// pragma comment(lib, "SDL2.lib")

int main() {

  int w = 800;
  int h = 600;

  // 1. 初始化 SDL Video 库
  if (SDL_Init(SDL_INIT_VIDEO)) {
    std::cout << SDL_GetError() << std::endl;
    return -1;
  }

  // 2. 生成 SDL 窗口
  auto screen = SDL_CreateWindow(
      "test sdl ffmpeg", SDL_WINDOWPOS_CENTERED, // 窗口位置
      SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

  if (!screen) {
    std::cout << SDL_GetError() << std::endl;
    return -1;
  }

  // 3. 生成渲染器
  auto render = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED);
  if (!render) {
    std::cout << SDL_GetError() << std::endl;
    return -1;
  }

  // 4. 生成材质
  auto texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, // 可加锁
                                   w, h);
  if (!texture) {
    std::cout << SDL_GetError() << std::endl;
    return -4;
  }

  // 存放图像的数据
  std::shared_ptr<unsigned char> rgb(new unsigned char[w * h * 4]);
  auto r = rgb.get(); // 取到裸指针，智能指针退出作用域自动释放指针

  unsigned char tmp = 255;

  for (;;) {
    // 判断退出
    SDL_Event ev;
    SDL_WaitEventTimeout(&ev, 10);
    if (ev.type == SDL_QUIT) {
      SDL_DestroyWindow(screen); // 超时销毁窗口
      break;
    }
    tmp--;
    for (int i = 0; i < h; ++i) {
      int b = i * w * 4;
      for (int j = 0; j < w * 4; j += 4) {
        r[b + j] = 0;       // B
        r[b + j + 1] = tmp; // G
        r[b + j + 2] = 0;   // R
        r[b + j + 3] = 0;   // A
      }
    }

    // 5. 内存数据写入材质
    SDL_UpdateTexture(texture, NULL, r, w * 4);
    // 材质
    // NULL 表示将图像的全部区域写入材质
    // r 表示具体的这个图像数据的指针
    // w * 4 表示每一行的字节数

    // 6. 清理屏幕
    SDL_RenderClear(render);
    SDL_Rect sdl_rect;

    sdl_rect.x = 0;
    sdl_rect.y = 0;
    sdl_rect.w = w;
    sdl_rect.h = h;

    // 7. 将材质拷贝到渲染器
    SDL_RenderCopy(render, texture,
                   NULL,     // 原图位置和尺寸
                   &sdl_rect // 目标位置和尺寸
    );

    // 8. 渲染
    SDL_RenderPresent(render);
  }
  getchar();
  return EXIT_SUCCESS;
}
