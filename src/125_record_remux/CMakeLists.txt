cmake_minimum_required(VERSION 3.16)
project(125_record_remux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()


get_filename_component(XFORMAT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.."
  ABSOLUTE)

set(XFORMAT_INCLUDE_DIR "${XFORMAT_ROOT}/include")
set(XFORMAT_LIB_DIR "${XFORMAT_ROOT}/lib")

# 包含头文件目录 (使用 BEFORE)
include_directories(BEFORE "${XFORMAT_INCLUDE_DIR}")

# 指定库文件目录
link_directories("${XFORMAT_LIB_DIR}")

# 设置 RPATH
set(CMAKE_BUILD_RPATH "${XFORMAT_LIB_DIR}")

# 设置项目文件
set(PROJECT_SOURCES
  125_record_remux.cpp
  xdemux.h
  xdemux.cpp
  xmux.h
  xmux.cpp
  xformat.h
  xformat.cpp
  xcodec.h
  xcodec.cpp
  xencode.h
  xencode.cpp
  xdecode.h
  xdecode.cpp
  xsws.h
  xsws.cpp
)


add_executable(125_record_remux ${PROJECT_SOURCES})

# 链接库
target_link_libraries(125_record_remux PRIVATE
  # SDL2
  # SDL2main
  avformat
  avcodec
  avutil
  swscale
  swresample
)
