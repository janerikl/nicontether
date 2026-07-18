cmake_minimum_required(VERSION 3.20)
project(NikonTether LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Optimize by default — the retouch pixel pipelines are far too slow unoptimized.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GPHOTO2 REQUIRED IMPORTED_TARGET libgphoto2)
pkg_check_modules(LIBRAW REQUIRED IMPORTED_TARGET libraw)

add_executable(nikontether
    src/main.cpp
    src/camera/CameraWorker.cpp
    src/camera/CameraController.cpp
    src/capture/SessionManager.cpp
    src/capture/NefPreview.cpp
    src/ui/MainWindow.cpp
    src/ui/LiveViewWidget.cpp
    src/ui/PannableScrollArea.cpp
    src/ui/ControlsPanel.cpp
    src/ui/FilmstripWidget.cpp
    src/ui/PreviewWindow.cpp
    src/edit/Adjustments.cpp
    src/edit/RawLoader.cpp
    src/edit/ImageCanvas.cpp
    src/edit/RetouchTab.cpp
    src/edit/RetouchWindow.cpp
    src/edit/ExportPreset.cpp
    src/edit/ExportDialog.cpp
    src/edit/CurveEditor.cpp
    src/edit/EditSidecar.cpp
)

target_include_directories(nikontether PRIVATE src)
target_link_libraries(nikontether PRIVATE
    Qt6::Widgets Qt6::Concurrent PkgConfig::GPHOTO2 PkgConfig::LIBRAW)

# `sudo cmake --install build` copies the binary to ${CMAKE_INSTALL_PREFIX}/bin
# (default /usr/local/bin).
include(GNUInstallDirs)
install(TARGETS nikontether RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Register CameraSettings metatypes are header-only; nothing extra needed.
