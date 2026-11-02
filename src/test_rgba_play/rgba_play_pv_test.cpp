// 生产者消费者模型
// std::condition_variable

#include <SDL2/SDL.h>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

#undef main

const char *filename = "setokai.rgba";
const int RGBA_WIDTH = 1280;
const int RGBA_HEIGHT = 720;
const int FRAME_SIZE = RGBA_WIDTH * RGBA_HEIGHT * 4;

// 线程安全的数据队列（存放读出来的帧数据）
std::queue<unsigned char *> frameQuque;
std::mutex queueMutex;
std::condition_variable queueCV;

// 用于子线程退出的标志
bool isExit = false;

// SDL 三个参数

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

  return 0;
}

void ThreadFunc(const char *filename) {}
