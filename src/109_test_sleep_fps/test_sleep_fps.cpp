#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono;

// 和 Windows 不同，Linux 上 的 clock() 是
// 进程消耗的 CPU时钟数，而不是 现实世界流逝的物理时间 墙上时间(Wall Time)
// 以及由于OS的调度粒度，sleep_for 天生不准
//
// void MSleep(unsigned int ms) {
//   auto beg = clock();
//   for (int i = 0; i < ms; ++i) {
//     std::this_thread::sleep_for(1ms);
//     if ((clock() - beg) / (CLOCKS_PER_SEC / 1000) >= ms)
//       break;
//   }
// }

// C++11 的单调时钟(std::chrono::steady_clock)，专门用来
// 测量时间流逝。
int main(int argc, char *argv[]) {

  // 1. 测试单次 sleep_for 精度
  std::cout << "--- 测试单次 sleep_for(10ms) 误差 ---" << std::endl;

  for (int i = 0; i < 5; ++i) {
    auto start = steady_clock::now();
    std::this_thread::sleep_for(10ms);
    auto end = steady_clock::now();

    // 计算实际流逝的毫秒数（带小数）
    auto diff =
        duration_cast<duration<double, std::milli>>(end - start).count();
    std::cout << "请求：10ms，实际：" << diff << " ms" << std::endl;
  }

  // 2. 测试 1 秒内能做多少次 sleep(10ms)
  std::cout << "\n--- 测试 1 秒内的循环次数 ---" << std::endl;
  auto beg = steady_clock::now();
  int fps = 0;
  while (true) {
    fps++;
    std::this_thread::sleep_for(10ms);

    auto now = steady_clock::now();
    auto elapsed_ms = duration_cast<milliseconds>(now - beg).count();

    if (elapsed_ms >= 1000) {
      std::cout << "理论上应该循环 100 次，实际循环：" << fps << " 次"
                << std::endl;
      break;
    }
  }

  // clock_t beg = clock(); // 开始时间，CPU跳数
  // int fps = 0;           // 帧率
  // std::this_thread::sleep_for(std::chrono::milliseconds(40));
  // for (;;) {
  //   fps++;
  //   auto tmp = clock();
  //   std::this_thread::sleep_for(10ms);
  //   std::cout << clock() - tmp << " " << std::flush;
  //   // 一秒钟开始统计 CLOCKS_PER_SEC 表示每秒跳数
  //   if ((clock() - beg) / (CLOCKS_PER_SEC / 1000000) > 1000) {
  //     // 间隔毫秒数
  //     std::cout << "sleep for fps: " << fps << std::endl;
  //     break;
  //   }
  // }
  // beg = clock();
  // fps = 0;

  // for (;;) {
  //   fps++;
  //   auto tmp = clock();
  //   MSleep(10);
  //   if ((clock() - beg) / (CLOCKS_PER_SEC / 1000000) > 1000) {
  //     std::cout << "MSleep fps: " << fps << std::endl;
  //     break;
  //   }
  // }

  getchar();

  return EXIT_SUCCESS;
}
