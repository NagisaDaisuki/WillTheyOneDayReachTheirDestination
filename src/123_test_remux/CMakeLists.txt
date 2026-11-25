cmake_minimum_required(VERSION 3.16)
project(123_test_remux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

get_filename_component(XDEMUX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.."
  ABSOLUTE)

set(XDEMUX_INCLUDE_DIR "${XDEMUX_ROOT}/include")
set(XDEMUX_LIB_DIR "${XDEMUX_ROOT}/lib")

# 包含头文件目录 (使用 BEFORE)
include_directories(BEFORE "${XDEMUX_INCLUDE_DIR}")

# 指定库文件目录
link_directories("${XDEMUX_LIB_DIR}")

# 设置 RPATH
set(CMAKE_BUILD_RPATH "${XDEMUX_LIB_DIR}")

# 设置项目文件
set(PROJECT_SOURCES
  123_test_remux.cpp
)


add_executable(123_test_remux ${PROJECT_SOURCES})

# 链接库
target_link_libraries(123_test_remux PRIVATE
  # SDL2
  # SDL2main
  avformat
  avcodec
  avutil
  swscale
  swresample
)
