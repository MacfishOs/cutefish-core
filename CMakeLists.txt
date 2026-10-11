cmake_minimum_required(VERSION 3.22)
project(core)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(QT Core Gui Quick QuickControls2 DBus Network LinguistTools)
find_package(Qt6 REQUIRED ${QT})
find_package(PkgConfig REQUIRED)

include(GNUInstallDirs)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

find_package(CutefishFramework REQUIRED COMPONENTS Application Capture Display Protocols)

add_subdirectory(appruntime)
add_subdirectory(booster)
add_subdirectory(polkit-agent)
add_subdirectory(privacy)
add_subdirectory(session)
add_subdirectory(cutefish-services)
add_subdirectory(screenrecorder)
