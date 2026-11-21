cmake_minimum_required(VERSION 3.16)
project(120_test_xdecode LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()


get_filename_component(XCODEC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(XCODEC_INCLUDE_DIR "${XCODEC_ROOT}/include")
set(XCODEC_LIB_DIR "${XCODEC_ROOT}/lib")

# 包含头文件目录 (使用 BEFORE)
include_directories(BEFORE "${XCODEC_INCLUDE_DIR}")

# 指定库文件目录
link_directories("${XCODEC_LIB_DIR}")

# 设置 RPATH
set(CMAKE_BUILD_RPATH "${XCODEC_LIB_DIR}")

# 设置项目所需文件
set(PROJECT_SOURCES
  120_test_xdecode.cpp
  xsdl.h
  xsdl.cpp
  xvideoview.h
  xvideoview.cpp
  xcodec.h
  xcodec.cpp
  xencode.h
  xencode.cpp
  xdecode.h
  xdecode.cpp
)

add_executable(120_test_xdecode ${PROJECT_SOURCES})

# 链接库
target_link_libraries(120_test_xdecode PRIVATE
  SDL2
  SDL2main
  avformat
  avcodec
  avutil
  swscale
  swresample
)
