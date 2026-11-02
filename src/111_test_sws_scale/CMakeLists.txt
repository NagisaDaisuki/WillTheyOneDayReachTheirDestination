cmake_minimum_required(VERSION 3.16)
project(111_test_sws_scale LANGUAGES CXX)

# 如果没有指定构建类型，则默认设为 Debug
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(SWS_SCALE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.."
            ABSOLUTE)
set(SWS_SCALE_INCLUDE_DIR "${SWS_SCALE_DIR}/include")
set(SWS_SCALE_LIB_DIR "${SWS_SCALE_DIR}/lib")

include_directories(BEFORE "${SWS_SCALE_INCLUDE_DIR}")
link_directories("${SWS_SCALE_LIB_DIR}")
set(CMAKE_BUILD_RPATH "${SWS_SCALE_LIB_DIR}")

add_executable(111_test_sws_scale 111_test_sws_scale.cpp)

target_link_libraries(111_test_sws_scale PRIVATE
  SDL2
  SDL2main
  avcodec
  avutil
  avformat
  swscale
  swresample
)
