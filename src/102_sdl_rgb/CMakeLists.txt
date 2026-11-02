cmake_minimum_required(VERSION 3.16)

project(102_sdl_rgb VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 如果需要用到 Qt库
find_package(Qt6 COMPONENTS Core Widgets REQUIRED)

# 定义根目录下 include 和 lib 的绝对路径
get_filename_component(SDL_RGB "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SDL_INCLUDE_DIR "${SDL_RGB}/include")
set(SDL_LIB_DIR "${SDL_RGB}/lib")

# 1. 包含头文件目录（使用 BEFORE优先于 A
# rch系统的 /usr/include）
include_directories(BEFORE "${SDL_INCLUDE_DIR}")

# 2. 指定库文件搜索目录
link_directories("${SDL_LIB_DIR}")

# 3. 设置 RPATH，运行时直接加载项目 lib
# 目录下的 .so, 防止加载到系统的新版 SDL
set(CMAKE_BUILD_RPATH "${SDL_LIB_DIR}")

add_executable(102_sdl_rgb 102_sdl_rgb.cpp)

target_link_libraries(102_sdl_rgb PRIVATE
  avformat
  avcodec
  avutil
  swscale
  swresample
  SDL2
  SDL2main
)



