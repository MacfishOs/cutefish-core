cmake_minimum_required(VERSION 3.22)
project(battery_warning_test LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
find_package(Qt6 REQUIRED COMPONENTS Core DBus Test)
find_package(CutefishFramework REQUIRED COMPONENTS Application)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
set(SERVICE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cutefish-services)
qt6_add_dbus_adaptor(ADAPTOR ${SERVICE_DIR}/battery/com.cutefish.Battery.xml
    ${SERVICE_DIR}/battery/battery.h Battery)
add_executable(battery_warning_test battery_warning_test.cpp
    ${SERVICE_DIR}/battery/battery.cpp
    ${SERVICE_DIR}/battery/batterywarning.cpp
    ${SERVICE_DIR}/battery/upowerdevice.cpp
    ${ADAPTOR})
target_include_directories(battery_warning_test PRIVATE ${SERVICE_DIR} ${SERVICE_DIR}/battery)
target_link_libraries(battery_warning_test PRIVATE Cutefish::Application Qt6::Core Qt6::DBus
    Qt6::Test)
enable_testing()
add_test(NAME battery_warning_test COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:battery_warning_test>)
