cmake_minimum_required(VERSION 3.16)

project(110_xvideo_view_avframe_fps VERSION 0.1 LANGUAGES CXX)

# 如果没有指定构建类型，则默认设为 Debug
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# XOpenGL 后端需要的 Qt OpenGL 组件（Qt6: OpenGLWidgets, Qt5: OpenGL）
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
  find_package(Qt6 REQUIRED COMPONENTS OpenGLWidgets)
else()
  find_package(Qt5 REQUIRED COMPONENTS OpenGL)
endif()

# 添加 SDL 头文件 和 库文件
get_filename_component(PLAY_YUV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.."
                    ABSOLUTE)
set(PLAY_YUV_INCLUDE_DIR "${PLAY_YUV_DIR}/include")
set(PLAY_YUV_LIB_DIR "${PLAY_YUV_DIR}/lib")

include_directories(BEFORE "${PLAY_YUV_INCLUDE_DIR}")

link_directories(BEFORE "${PLAY_YUV_LIB_DIR}")

set(CMAKE_BUILD_RPATH "${PLAY_YUV_LIB_DIR}")

set(PROJECT_SOURCES
        main.cpp
        sdlplayyuv.cpp
        sdlplayyuv.h
        xvideoview.cpp
        xvideoview.h
        xsdl.cpp
        xsdl.h
        sdlplayyuv.ui
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
  qt_add_executable(110_xvideo_view_avframe_fps
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
  # Define target properties for Android with Qt 6 as:
  #    set_property(TARGET 110_xvideo_view_avframe_fps APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
  #                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
  # For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
  if(ANDROID)
    add_library(110_xvideo_view_avframe_fps SHARED
            ${PROJECT_SOURCES}
        )
    # Define properties for Android with Qt 5 after find_package() calls as:
    #    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
  else()
    add_executable(110_xvideo_view_avframe_fps
            ${PROJECT_SOURCES}
        )
  endif()
endif()

target_link_libraries(110_xvideo_view_avframe_fps PRIVATE
  Qt${QT_VERSION_MAJOR}::Widgets
  # 链接 SDL2
  SDL2
  SDL2main
  # FFmpeg
  avcodec
  avformat
  avutil
  swscale
  swresample
)

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
  target_link_libraries(110_xvideo_view_avframe_fps PRIVATE Qt6::OpenGLWidgets)
else()
  target_link_libraries(110_xvideo_view_avframe_fps PRIVATE Qt5::OpenGL)
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.110_xvideo_view_avframe_fps)
endif()
set_target_properties(110_xvideo_view_avframe_fps PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS 110_xvideo_view_avframe_fps
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
  qt_finalize_executable(110_xvideo_view_avframe_fps)
endif()
