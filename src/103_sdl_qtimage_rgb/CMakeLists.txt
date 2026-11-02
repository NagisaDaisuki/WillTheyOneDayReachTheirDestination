cmake_minimum_required(VERSION 3.16)

project(103_sdl_qtimage_rgb VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# 定义根目录下 include 和 lib 的绝对路径
get_filename_component(SDL_QT_RGB "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SDL_QT_INCLUDE_DIR "${SDL_QT_RGB}/include")
set(SDL_QT_LIB_DIR "${SDL_QT_RGB}/lib")

# 包含头文件目录
include_directories(BEFORE "${SDL_QT_INCLUDE_DIR}")

# 指定库文件搜索目录
link_directories("${SDL_QT_LIB_DIR}")

# 设置 RPATH，运行时直接加载项目 lib
# 目录下的 .so, 防止加载到系统的新版 SDL
set(CMAKE_BUILD_RPATH "${SDL_QT_LIB_DIR}")

set(PROJECT_SOURCES
        main.cpp
        sdlqtimagergb.cpp
        sdlqtimagergb.h
        sdlqtimagergb.ui
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(103_sdl_qtimage_rgb
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET 103_sdl_qtimage_rgb APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(103_sdl_qtimage_rgb SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(103_sdl_qtimage_rgb
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(103_sdl_qtimage_rgb PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
        # 引入 SDL2库
    SDL2
    SDL2main
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.103_sdl_qtimage_rgb)
endif()
set_target_properties(103_sdl_qtimage_rgb PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS 103_sdl_qtimage_rgb
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(103_sdl_qtimage_rgb)
endif()
