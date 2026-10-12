cmake_minimum_required(VERSION 3.22)
project(night_light_test LANGUAGES C CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
find_package(Qt6 REQUIRED COMPONENTS Core DBus Test)
find_package(CutefishFramework REQUIRED COMPONENTS Application Display)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
set(SERVICE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cutefish-services)
qt6_add_dbus_adaptor(ADAPTOR_SOURCES ${SERVICE_DIR}/nightlight/com.cutefish.Services.NightLight.xml
    ${SERVICE_DIR}/nightlight/nightlight.h NightLight)
add_executable(night_light_test night_light_test.cpp ${ADAPTOR_SOURCES}
    ${SERVICE_DIR}/nightlight/nightlight.cpp
    ${SERVICE_DIR}/schedule/scheduledfeature.cpp
    ${SERVICE_DIR}/schedule/scheduleutils.cpp)
target_include_directories(night_light_test PRIVATE ${SERVICE_DIR} ${SERVICE_DIR}/nightlight
    ${SERVICE_DIR}/schedule)
target_link_libraries(night_light_test PRIVATE Cutefish::Application Cutefish::Display Qt6::Core
    Qt6::DBus Qt6::Test)
enable_testing()
add_test(NAME night_light_test COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:night_light_test>)
