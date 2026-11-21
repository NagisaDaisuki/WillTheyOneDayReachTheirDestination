cmake_minimum_required(VERSION 3.16)
project(120_test_decode_view_hw_no_copy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")

# 包含头文件目录 (使用 BEFORE)
include_directories(BEFORE "${FFMPEG_INCLUDE_DIR}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLX REQUIRED gl)
pkg_check_modules(VA REQUIRED libva)
pkg_check_modules(DRM REQUIRED libdrm)

# 必须用系统的 SDL2。项目自带的 SDL2 是 2.0.12，没有 Wayland/EGL 后端，
# 拿到的不是 EGLDisplay，就没法导入 dma_buf，零拷贝走不通
find_library(SDL2_SYSTEM_LIB NAMES SDL2-2.0
  PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib
  NO_DEFAULT_PATH)
if (NOT SDL2_SYSTEM_LIB)
  message(FATAL_ERROR "找不到系统 SDL2（需要 Wayland/EGL 支持，如 sdl2-compat）")
endif()

add_executable(120_test_decode_view_hw_no_copy
  120_test_decode_view_hw_no_copy.cpp
  glview.cpp
)

target_include_directories(120_test_decode_view_hw_no_copy PRIVATE
  /usr/include/SDL2
  ${EGL_INCLUDE_DIRS}
  ${GLX_INCLUDE_DIRS}
  ${VA_INCLUDE_DIRS}
  ${DRM_INCLUDE_DIRS}
)

target_link_directories(120_test_decode_view_hw_no_copy PRIVATE "${FFMPEG_LIB_DIR}")

target_link_libraries(120_test_decode_view_hw_no_copy PRIVATE
  ${SDL2_SYSTEM_LIB}
  avformat
  avcodec
  avutil
  ${EGL_LIBRARIES}
  ${GLX_LIBRARIES}
  ${VA_LIBRARIES}
)

# /usr/lib 放在 RPATH 最前面，防止运行时把项目自带的 libSDL2-2.0.so.0(2.0.12) 加载进来；
# 用老式 DT_RPATH 才能让 libavformat 的间接依赖（libswresample 等）也找到
set_target_properties(120_test_decode_view_hw_no_copy PROPERTIES
  BUILD_WITH_INSTALL_RPATH ON
  INSTALL_RPATH "/usr/lib;${FFMPEG_LIB_DIR}"
)
target_link_options(120_test_decode_view_hw_no_copy PRIVATE "-Wl,--disable-new-dtags")
