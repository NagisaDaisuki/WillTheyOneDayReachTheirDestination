// 生产者消费者模型
// std::condition_variable

#include <SDL2/SDL.h>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#undef main

const char *filename = "setokai_rgba.rgb";
const int RGBA_WIDTH = 1280;
const int RGBA_HEIGHT = 720;
const int FRAME_SIZE = RGBA_WIDTH * RGBA_HEIGHT * 4;

// 线程安全的数据队列（存放读出来的帧数据）
std::queue<unsigned char *> frameQueue;
std::mutex queueMutex;
std::condition_variable queueCV;

// 用于子线程退出的标志
bool isExit = false;

// 带有状态的删除器结构体
// 删除器
struct SDLWindowDeleter {
  void operator()(SDL_Window *w) const {
    if (w)
      SDL_DestroyWindow(w);
  }
};

// Lambda 表达式（需要显式初始化删除器）
auto rendererDeleter = [](SDL_Renderer *renderer) {
  if (renderer)
    SDL_DestroyRenderer(renderer);
};

// SDL 三个参数
static std::unique_ptr<SDL_Window, SDLWindowDeleter> win;
static std::unique_ptr<SDL_Renderer, decltype(rendererDeleter)>
    renderer(nullptr, rendererDeleter);
static std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>
    texture(nullptr, SDL_DestroyTexture);

// 线程变量
static std::thread readThread{};

// 子线程函数
void ThreadFunc(const char *filename);

int main(int argc, char **argv) {

  std::cout << "Multi-thread rgba player..." << std::endl;

  // 1. 初始化 SDL
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "Failed to init SDL: " << SDL_GetError() << std::endl;
    return -1;
  }

  // 2. 初始化窗口
  win.reset(SDL_CreateWindow("Multi-thread RGBA player", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, RGBA_WIDTH, RGBA_HEIGHT,
                             SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE));
  if (!win.get()) {
    std::cerr << "SDL Create Window failed: " << SDL_GetError() << std::endl;
    SDL_Quit();
    return -1;
  }

  // 设置缩放渲染窗口：线性插值法
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

  // 3. 创建渲染器
  renderer.reset(SDL_CreateRenderer(win.get(), -1, SDL_RENDERER_ACCELERATED));
  if (!renderer) {
    renderer.reset(SDL_CreateRenderer(win.get(), -1, SDL_RENDERER_SOFTWARE));
  }
  if (!renderer) {
    std::cerr << "SDL Create Renderer failed: " << SDL_GetError() << std::endl;
    // SDL_DestroyWindow(win.get());
    SDL_Quit();
    return -1;
  }

  // 4. 创建材质
  texture.reset(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ABGR8888,
                                  SDL_TEXTUREACCESS_STREAMING, RGBA_WIDTH,
                                  RGBA_HEIGHT));
  if (!texture) {
    std::cerr << "SDL Create Texture failed: " << SDL_GetError() << std::endl;
    // SDL_DestroyRenderer(renderer.get());
    // SDL_DestroyWindow(win.get());
    SDL_Quit();
    return -1;
  }

  // 5. 启动子线程 读取 RGBA 文件
  readThread = std::thread(ThreadFunc, filename);

  // 准备变量
  bool quit = false;
  SDL_Event ev;
  const int FPS = 25;
  const int FRAME_DELAY = 1000 / FPS;
  Uint32 frameStart{};

  // 6. 主线程循环渲染
  while (!quit) {
    frameStart = SDL_GetTicks();

    // 事件处理
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT)
        quit = true;
    }

    // 从队列获取数据
    unsigned char *renderBuffer = nullptr;
    bool hasFrameGet = false;
    {
      std::unique_lock<std::mutex> lock(queueMutex);
      if (!frameQueue.empty()) {
        renderBuffer = frameQueue.front();
        frameQueue.pop();
        hasFrameGet = true;
      }
    }
    if (hasFrameGet)
      queueCV.notify_one();

    // 拿到了新的一帧，就更新纹理并释放旧内存
    if (renderBuffer) {
      SDL_UpdateTexture(texture.get(), nullptr, renderBuffer, RGBA_WIDTH * 4);
      delete[] renderBuffer;
    }

    // 渲染
    SDL_RenderClear(renderer.get());
    SDL_RenderCopy(renderer.get(), texture.get(), nullptr, nullptr);
    SDL_RenderPresent(renderer.get());

    // 动态控制延时
    Uint32 elapsed = SDL_GetTicks() - frameStart;
    if (elapsed < FRAME_DELAY) {
      SDL_Delay(FRAME_DELAY - elapsed);
    }
  }

  // 退出清理逻辑
  isExit = true;        // 通知子线程退出
  queueCV.notify_all(); // 唤醒可能卡在wait里的子线程

  if (readThread.joinable())
    readThread.join(); // 等待子线程安全退出

  // 清空队列里的 20 帧内存，防止内存泄露
  {
    std::unique_lock<std::mutex> lock(queueMutex);
    while (!frameQueue.empty()) {
      delete[] frameQueue.front();
      frameQueue.pop();
    }
  }

  SDL_Quit(); // 释放SDL 底层库
  return 0;
}

void ThreadFunc(const char *filename) {
  // 读取文件的 输入文件流
  std::ifstream ifs;
  if (ifs.is_open())
    ifs.close();
  ifs.open(filename, std::ios::binary);
  if (!ifs) {
    std::cerr << "failed to open file: " << filename << std::endl;
    ifs.close();
    return;
  }

  while (!isExit) {
    // 限制队列最大长度，防止读的太快爆内存（最多存20 帧）
    {
      std::unique_lock<std::mutex> lock(queueMutex);
      queueCV.wait(lock, [] { return frameQueue.size() < 20 || isExit; });
      if (isExit)
        break;
    }
    unsigned char *buffer = new unsigned char[FRAME_SIZE];
    ifs.read((char *)buffer, FRAME_SIZE);

    // 循环播放逻辑
    if (ifs.gcount() < FRAME_SIZE) {
      ifs.clear();
      ifs.seekg(0, std::ios::beg);
      ifs.read((char *)buffer, FRAME_SIZE);
    }

    // 将读到的数据压入队列
    {
      std::unique_lock<std::mutex> lock(queueMutex);
      frameQueue.push(buffer);
    }
    queueCV.notify_all(); // 唤醒主线程
  }
}
