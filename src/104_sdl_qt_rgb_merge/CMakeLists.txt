cmake_minimum_required(VERSION 3.16)

project(104_sdl_qt_rgb_merge VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# 引入 include 和 lib
get_filename_component(SDL_QT_MERGE "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SDL_QT_MERGE_INCLUDE "${SDL_QT_MERGE}/include")
set(SDL_QT_MERGE_LIB "${SDL_QT_MERGE}/lib")

set(PROJECT_SOURCES
        main.cpp
        sdlqtmergepic.cpp
        sdlqtmergepic.h
        sdlqtmergepic.ui
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(104_sdl_qt_rgb_merge
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET 104_sdl_qt_rgb_merge APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(104_sdl_qt_rgb_merge SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(104_sdl_qt_rgb_merge
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(104_sdl_qt_rgb_merge PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
        # 引入 SDL2
        SDL2
    SDL2main
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.104_sdl_qt_rgb_merge)
endif()
set_target_properties(104_sdl_qt_rgb_merge PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS 104_sdl_qt_rgb_merge
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(104_sdl_qt_rgb_merge)
endif()
