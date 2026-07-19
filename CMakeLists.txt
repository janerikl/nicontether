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
    src/ui/TetherView.cpp
    src/ui/LiveViewWidget.cpp
    src/ui/PannableScrollArea.cpp
    src/ui/ControlsPanel.cpp
    src/ui/PreferencesDialog.cpp
    src/ui/FilmstripWidget.cpp
    src/ui/HistogramWidget.cpp
    src/ui/LevelsPanel.cpp
    src/ui/MaskPanel.cpp
    src/ui/LayersPanel.cpp
    src/ui/ToolFlyout.cpp
    src/ui/AppIcon.cpp
    src/edit/Adjustments.cpp
    src/edit/RawLoader.cpp
    src/edit/ImageCanvas.cpp
    src/edit/RetouchTab.cpp
    src/edit/RetouchWindow.cpp
    src/edit/ExportPreset.cpp
    src/edit/RecentSessions.cpp
    src/edit/ExportDialog.cpp
    src/edit/CurveEditor.cpp
    src/edit/EditSidecar.cpp
    src/edit/HealTool.cpp
)

target_include_directories(nikontether PRIVATE src)
target_link_libraries(nikontether PRIVATE
    Qt6::Widgets Qt6::Concurrent PkgConfig::GPHOTO2 PkgConfig::LIBRAW)

# `sudo cmake --install build` copies the binary to ${CMAKE_INSTALL_PREFIX}/bin
# (default /usr/local/bin).
include(GNUInstallDirs)
install(TARGETS nikontether RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Desktop launcher entry.
install(FILES packaging/nikontether.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# Generate the launcher PNG from the same drawing code as the window icon.
# Uses the offscreen Qt platform so it works on headless installs.
install(CODE "
    set(ICON_DIR \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps\")
    file(MAKE_DIRECTORY \"\${ICON_DIR}\")
    execute_process(
        COMMAND \${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen
                \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/nikontether\" --export-icon \"\${ICON_DIR}/nikontether.png\"
        RESULT_VARIABLE _rc
    )
    if(NOT _rc EQUAL 0)
        message(WARNING \"Failed to generate nikontether.png (code \${_rc})\")
    endif()
")

# Register CameraSettings metatypes are header-only; nothing extra needed.

enable_testing()
add_executable(af_mapping_test tests/AfMappingTest.cpp)
target_include_directories(af_mapping_test PRIVATE src)
add_test(NAME af_mapping_test COMMAND af_mapping_test)
add_executable(cam_models_test tests/CameraModelsTest.cpp)
target_include_directories(cam_models_test PRIVATE src)
add_test(NAME cam_models_test COMMAND cam_models_test)
add_executable(af_calibrator_test tests/AfCalibratorTest.cpp)
target_include_directories(af_calibrator_test PRIVATE src)
add_test(NAME af_calibrator_test COMMAND af_calibrator_test)
add_executable(grid_overlay_test tests/GridOverlayTest.cpp)
target_include_directories(grid_overlay_test PRIVATE src)
add_test(NAME grid_overlay_test COMMAND grid_overlay_test)
