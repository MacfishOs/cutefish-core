cmake_minimum_required(VERSION 3.22)
project(power_policy_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(CutefishToolkit REQUIRED)
find_package(CutefishClients REQUIRED COMPONENTS display)
find_package(GTest REQUIRED)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
set(SERVICE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cutefish-services)
add_subdirectory(${SERVICE_DIR}/idle ${CMAKE_CURRENT_BINARY_DIR}/idle)
add_executable(power_policy_test power_policy_test.cpp
    ${SERVICE_DIR}/log.cpp
    ${SERVICE_DIR}/brightness/brightnessmanager.cpp
    ${SERVICE_DIR}/brightness/ddcworker.cpp
    ${SERVICE_DIR}/power/action.cpp
    ${SERVICE_DIR}/power/dimdisplayaction.cpp
    ${SERVICE_DIR}/power/idlemanager.cpp)
target_include_directories(power_policy_test PRIVATE ${SERVICE_DIR} ${SERVICE_DIR}/power
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(power_policy_test PRIVATE Cutefish::DisplayClient cutefish-idle-backend
    Cutefish::ToolkitBase GTest::gtest_main)
enable_testing()
add_test(NAME power_policy_test COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:power_policy_test>)
