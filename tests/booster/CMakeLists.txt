cmake_minimum_required(VERSION 3.22)
project(booster_test LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Test)
find_package(CutefishFramework REQUIRED COMPONENTS Application)
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(cutefish-booster ${CORE_DIR}/booster/main.cpp ${CORE_DIR}/booster/preload.qrc)
target_include_directories(cutefish-booster PRIVATE ${CORE_DIR})
target_link_libraries(cutefish-booster PRIVATE Cutefish::Application Qt6::CorePrivate Qt6::Gui
    Qt6::Qml Qt6::Quick)

# What the application's side of the launch looks like from inside.
add_library(booster-test-application MODULE application.cpp)
target_link_libraries(booster-test-application PRIVATE Qt6::Gui)
add_library(booster-test-no-entry MODULE noentry.cpp)

add_executable(booster_test booster_test.cpp)
target_include_directories(booster_test PRIVATE ${CORE_DIR})
target_link_libraries(booster_test PRIVATE Qt6::Core Qt6::Test)
target_compile_definitions(booster_test PRIVATE
    BOOSTER_PATH="$<TARGET_FILE:cutefish-booster>"
    APPLICATION_MODULE="$<TARGET_FILE:booster-test-application>"
    NO_ENTRY_MODULE="$<TARGET_FILE:booster-test-no-entry>")
add_dependencies(booster_test cutefish-booster booster-test-application booster-test-no-entry)
enable_testing()
add_test(NAME booster_test COMMAND booster_test)
