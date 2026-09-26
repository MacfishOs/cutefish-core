cmake_minimum_required(VERSION 3.22)
project(power_lid_test LANGUAGES C CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
find_package(Qt6 REQUIRED COMPONENTS Core DBus Test)
find_package(CutefishFramework REQUIRED COMPONENTS Display)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
set(SERVICE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cutefish-services)
add_subdirectory(${SERVICE_DIR}/idle ${CMAKE_CURRENT_BINARY_DIR}/idle)
add_executable(power_lid_test power_lid_test.cpp
    ${SERVICE_DIR}/power/action.cpp
    ${SERVICE_DIR}/power/dimdisplayaction.cpp
    ${SERVICE_DIR}/power/idlemanager.cpp)
target_include_directories(power_lid_test PRIVATE ${SERVICE_DIR} ${SERVICE_DIR}/power)
target_link_libraries(power_lid_test PRIVATE Cutefish::Display cutefish-idle-backend Qt6::Core
    Qt6::DBus Qt6::Test)
enable_testing()
add_test(NAME power_lid_test COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:power_lid_test>)
