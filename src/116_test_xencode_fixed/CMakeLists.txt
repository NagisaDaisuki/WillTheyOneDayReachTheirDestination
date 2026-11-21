cmake_minimum_required(VERSION 3.16)
project(116_test_xencode_fixed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# [CLEANUP] 默认 Debug（-g -O0）：这是能用 gdb 定位到具体行号的前提。
#   原来没指定 build type，编译产物没有调试符号，gdb 里 `bt` 只能看到
#   "0x555555557807 in main ()" 和 "No symbol table is loaded"，无从定位。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# [CLEANUP] Qt6 未使用，不再 find_package(Qt6 ...)，避免多一个可选依赖。

# 定义根目录下 include 和 lib 的绝对路径
get_filename_component(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")

# 1. 包含头文件目录（使用 BEFORE，优先于 Arch 系统的 /usr/include）
include_directories(BEFORE "${FFMPEG_INCLUDE_DIR}")

# 2. 指定库文件搜索目录
link_directories("${FFMPEG_LIB_DIR}")

# 3. 【关键】RPATH：运行时直接加载项目 lib 目录下的 .so，
#    防止加载到系统的新版 FFmpeg。build 树和 install 后都要生效。
set(CMAKE_BUILD_RPATH "${FFMPEG_LIB_DIR}")
set(CMAKE_INSTALL_RPATH "${FFMPEG_LIB_DIR}")

# 4. 设置项目所需文件
set(PROJECT_SOURCES
    116_test_xencode.cpp
    xencode.h
    xencode.cpp
)

add_executable(116_test_xencode ${PROJECT_SOURCES})

# 5. 链接库（只链真正用到的：avcodec + avutil）
target_link_libraries(116_test_xencode PRIVATE
  avcodec
  avutil
)

# [CLEANUP] 强制使用 DT_RPATH 而非默认的 DT_RUNPATH。
#   区别很关键：DT_RUNPATH 只对「本对象的直接依赖」生效，所以 libavcodec.so.58
#   自己的依赖（libswresample.so.3 等）不会去搜我们项目的 lib 目录，运行时报
#   "libswresample.so.3: cannot open shared object file"。
#   改成 DT_RPATH 后，ld.so 会把该路径传递给传递性依赖的查找。
target_link_options(116_test_xencode PRIVATE -Wl,--disable-new-dtags)

# [CLEANUP] 开编译警告，尽早暴露 c->width 之类的可疑用法。
target_compile_options(116_test_xencode PRIVATE -Wall -Wextra)
