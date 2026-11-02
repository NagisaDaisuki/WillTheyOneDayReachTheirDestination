cmake_minimum_required(VERSION 3.16)
project(minimum_sdl VERSION 0.01 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


get_filename_component(MINIMUM_SDL "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(MINIMUM_SDL_INCLUDE "${MINIMUM_SDL}/include")
set(MINIMUM_SDL_LIB "${MINIMUM_SDL}/lib")

include_directories(BEFORE "${MINIMUM_SDL_INCLUDE}")

link_libraries("${MINIMUM_SDL_LIB}")

set(CMAKE_BUILD_RPATH "${MINIMUM_SDL_LIB}")

add_executable(minimum_sdl minimum_sdl_test.cpp)

target_link_libraries(minimum_sdl PRIVATE
  SDL2
  SDL2main
)

