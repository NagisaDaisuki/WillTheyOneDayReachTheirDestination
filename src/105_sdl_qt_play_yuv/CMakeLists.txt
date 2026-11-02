cmake_minimum_required(VERSION 3.16)

project(105_sdl_qt_play_yuv VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# 添加 SDL 头文件 和 库文件
get_filename_component(PLAY_YUV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(PLAY_YUV_INCLUDE_DIR "${PLAY_YUV_DIR}/include")
set(PLAY_YUV_LIB_DIR "${PLAY_YUV_DIR}/lib")

include_directories(BEFORE "${PLAY_YUV_INCLUDE_DIR}")

link_directories(BEFORE "${PLAY_YUV_LIB_DIR}")

set(CMAKE_BUILD_RPATH "${PLAY_YUV_LIB_DIR}")

set(PROJECT_SOURCES
        main.cpp
        sdlplayyuv.cpp
        sdlplayyuv.h
        sdlplayyuv.ui
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
  qt_add_executable(105_sdl_qt_play_yuv
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
  # Define target properties for Android with Qt 6 as:
  #    set_property(TARGET 105_sdl_qt_play_yuv APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
  #                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
  # For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
  if(ANDROID)
    add_library(105_sdl_qt_play_yuv SHARED
            ${PROJECT_SOURCES}
        )
    # Define properties for Android with Qt 5 after find_package() calls as:
    #    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
  else()
    add_executable(105_sdl_qt_play_yuv
            ${PROJECT_SOURCES}
        )
  endif()
endif()

target_link_libraries(105_sdl_qt_play_yuv PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
  # 链接 SDL2
  SDL2
  SDL2main
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.105_sdl_qt_play_yuv)
endif()
set_target_properties(105_sdl_qt_play_yuv PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS 105_sdl_qt_play_yuv
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
  qt_finalize_executable(105_sdl_qt_play_yuv)
endif()
