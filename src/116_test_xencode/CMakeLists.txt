cmake_minimum_required(VERSION 3.16)
project(116_test_xencode LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 如果需要用到 Qt库
find_package(Qt6 COMPONENTS Core Widgets REQUIRED)

# 定义根目录下 include 和 lib 的绝对路径
get_filename_component(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")

# 1. 包含头文件目录（使用 BEFORE
#   优先于 Arch 系统的 /usr/include）
include_directories(BEFORE "${FFMPEG_INCLUDE_DIR}")

# 2. 指定库文件搜索目录
link_directories("${FFMPEG_LIB_DIR}")

# 3. 【关键】设置RPATH，运行时直接加载项目 lib
# 目录下的 .so，防止加载到系统的新版 FFmpeg
set(CMAKE_INSTALL_RPATH "${FFMPEG_LIB_DIR}")

# 4. 设置 项目所需文件
set(PROJECT_SOURCES
    116_test_xencode.cpp
    xencode.h
    xencode.cpp
)

add_executable(116_test_xencode
${PROJECT_SOURCES})

# 4. 链接库
target_link_libraries(116_test_xencode PRIVATE
  # Qt6::Core Qt6::Widgets  # 若不用 Qt 可去掉
  avformat
  avcodec
  avutil
  swscale
  swresample
  # fdk-aac
  # x264
  # x265
)
