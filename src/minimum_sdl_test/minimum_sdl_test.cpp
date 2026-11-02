#include <SDL2/SDL.h>
#include <iostream>
#include <vector>

namespace my_sdl {

const int WIDTH = 800;
const int HEIGHT = 600;
SDL_Window *window = nullptr;
SDL_Renderer *renderer = nullptr;
SDL_Texture *texture = nullptr;
} // namespace my_sdl

int main(int argc, char *argv[]) {
  // 1. 初始化 SDL 视频 子系统
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "SDL Init Failed: " << SDL_GetError() << std::endl;
    return -1;
  }

  // 2. SDL 自己创建一个顶层窗口
  my_sdl::window = SDL_CreateWindow("Pure SDL RGB Test", SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED, my_sdl::WIDTH,
                                    my_sdl::HEIGHT, SDL_WINDOW_SHOWN);

  // 3. 创建渲染器（自动适配底层，Wayland / X11 / Windows 均可）
  my_sdl::renderer = SDL_CreateRenderer(
      my_sdl::window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  // 4. 创建材质（用来承载 RGB 数据）
  my_sdl::texture = SDL_CreateTexture(
      my_sdl::renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
      my_sdl::WIDTH, my_sdl::HEIGHT);

  std::vector<unsigned char> rgb(my_sdl::WIDTH * my_sdl::HEIGHT *
                                 4); // 4 字节：ARGB
  bool running = true;
  SDL_Event event;
  int tmp = 255;

  // 5. SDL 的标准游戏主循环
  while (running) {
    // 事件处理
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT)
        running = false;
    }

    // 更新数据（颜色渐变）
    tmp--;
    if (tmp < 0)
      tmp = 255;
    for (int j = 0; j < my_sdl::HEIGHT; ++j) {
      int b = j * my_sdl::WIDTH * 4;
      for (int i = 0; i < my_sdl::WIDTH * 4; i += 4) {
        rgb[b + i] = 0;
        rgb[b + i + 1] = tmp;
        rgb[b + i + 2] = 0;
        rgb[b + i + 3] = 255;
      }
    }

    // 将数据推入材质
    SDL_UpdateTexture(my_sdl::texture, NULL, rgb.data(), my_sdl::WIDTH * 4);

    // 清屏 -> 拷贝 -> 提交（渲染三部曲）
    SDL_RenderClear(my_sdl::renderer);
    SDL_RenderCopy(my_sdl::renderer, my_sdl::texture, NULL, NULL);
    SDL_RenderPresent(my_sdl::renderer);

    // 控制帧率（因为开了 VSYNC 这里其实可以省略，但为了简单可以保留）
    SDL_Delay(10);
  }

  // 6. 清理资源
  SDL_DestroyTexture(my_sdl::texture);
  SDL_DestroyRenderer(my_sdl::renderer);
  SDL_DestroyWindow(my_sdl::window);
  SDL_Quit();

  return EXIT_SUCCESS;
}
