cmake_minimum_required(VERSION 3.16)
project(116_test_xencode_modern LANGUAGES CXX)

# 需要 C++23：std::expected、std::span、[[nodiscard]] 等
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# FFmpeg（沿用项目自带的 include/ 与 lib/，优先于系统版本）
# ---------------------------------------------------------------------------
get_filename_component(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")

# 用 imported target 代替老式的 include_directories / link_directories：
# 依赖关系显式挂在 target 上，谁用谁继承，不会污染全局编译环境。
add_library(ffmpeg_shared SHARED IMPORTED)
set_target_properties(ffmpeg_shared PROPERTIES
  IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/libavcodec.so"
  INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
)

# ---------------------------------------------------------------------------
# 库：xencode
#
# PUBLIC 头文件目录只暴露 include/，src/ 下的内部实现（detail/ffi.h）
# 对外完全不可见 —— 这就是"接口暴露更少"在构建系统层面的体现。
# ---------------------------------------------------------------------------
add_library(xencode STATIC
  src/errors.cpp
  src/frame.cpp
  src/packet.cpp
  src/encoder.cpp
  src/bitstream_writer.cpp
)

target_include_directories(xencode
  PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# FFmpeg 头文件只在 PRIVATE 里给库自己用，不传递给使用 xencode 的人。
target_link_libraries(xencode
  PRIVATE avcodec avutil
)
target_include_directories(xencode PRIVATE "${FFMPEG_INCLUDE_DIR}")
target_link_directories(xencode PRIVATE "${FFMPEG_LIB_DIR}")

target_compile_options(xencode PRIVATE -Wall -Wextra -Wpedantic -Wshadow)

# ---------------------------------------------------------------------------
# 可执行文件：demo
#
# 注意 demo 只链接 xencode，不链接任何 av* 库 —— ：
# 它连 FFmpeg 的头文件都看不到，物理上不可能写出裸 av_* 调用。
# ---------------------------------------------------------------------------
add_executable(xencode_demo demo/main.cpp)
target_link_libraries(xencode_demo PRIVATE xencode)

# DT_RPATH（而非默认的 DT_RUNPATH）才能让 libavcodec 的传递依赖
# （libswresample/libswscale ...）也在项目的 lib/ 下找到。
target_link_directories(xencode_demo PRIVATE "${FFMPEG_LIB_DIR}")
target_link_options(xencode_demo PRIVATE -Wl,--disable-new-dtags)
set_target_properties(xencode_demo PROPERTIES
  BUILD_RPATH   "${FFMPEG_LIB_DIR}"
  INSTALL_RPATH "${FFMPEG_LIB_DIR}"
)
