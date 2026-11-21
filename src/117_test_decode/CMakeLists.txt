cmake_minimum_required(VERSION 3.16)
project(117_test_decode LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")

# 包含头文件目录 (使用 BEFORE)
include_directories(BEFORE "${FFMPEG_INCLUDE_DIR}")

# 指定库文件目录
link_directories("${FFMPEG_LIB_DIR}")

# 设置 RPATH
set(CMAKE_INSTALL_RPATH "${FFMPEG_LIB_DIR}")

# 设置项目所需文件
set(PROJECT_SOURCES
  117_test_decode.cpp
)

add_executable(117_test_decode ${PROJECT_SOURCES})

# 链接库
target_link_libraries(117_test_decode PRIVATE
  avformat
  avcodec
  avutil
  swscale
  swresample
)
