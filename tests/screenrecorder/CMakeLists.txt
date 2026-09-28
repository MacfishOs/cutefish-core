cmake_minimum_required(VERSION 3.22)
project(screenrecorder_encoder_test LANGUAGES C CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Test)
find_package(CutefishFramework REQUIRED COMPONENTS Capture)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFmpeg REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswscale)
pkg_check_modules(LibVA REQUIRED IMPORTED_TARGET libva)
pkg_check_modules(Libdrm REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(Gbm REQUIRED IMPORTED_TARGET gbm)
set(RECORDER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../screenrecorder)
# The encoder, fed made-up frames and sound, and DMA-BUFs where a render node can
# allocate them; the files it writes are read back with FFmpeg.
add_executable(screenrecorder_encoder_test screenrecorder_encoder_test.cpp
    ${RECORDER_DIR}/encoder.cpp)
target_include_directories(screenrecorder_encoder_test PRIVATE ${RECORDER_DIR})
target_link_libraries(screenrecorder_encoder_test PRIVATE Cutefish::Capture Qt6::Gui Qt6::Test
    PkgConfig::FFmpeg PkgConfig::LibVA PkgConfig::Libdrm PkgConfig::Gbm)
enable_testing()
add_test(NAME screenrecorder_encoder_test COMMAND screenrecorder_encoder_test)
