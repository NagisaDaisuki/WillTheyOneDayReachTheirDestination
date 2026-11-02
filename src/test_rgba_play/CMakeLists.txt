cmake_minimum_required(VERSION 3.16)
project(rgba_play_pv CXX)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(RGBA_PLAY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.."
                      ABSOLUTE)
set(RGBA_PLAY_INCLUDE_DIR "${RGBA_PLAY_DIR}/include")
set(RGBA_PLAY_LIB_DIR "${RGBA_PLAY_DIR}/lib")

include_directories(BEFORE "${RGBA_PLAY_INCLUDE_DIR}")
link_directories(BEFORE "${RGBA_PLAY_LIB_DIR}")
set(CMAKE_BUILD_RPATH "${RGBA_PLAY_LIB_DIR}")

add_executable(rgba_play_pv rgba_play_pv.cpp)

target_link_libraries(rgba_play_pv PRIVATE
  SDL2
  SDL2main
)



