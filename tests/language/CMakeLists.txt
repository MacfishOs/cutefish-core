cmake_minimum_required(VERSION 3.22)
project(language_service_test LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui DBus Test)
find_package(CutefishFramework REQUIRED COMPONENTS Applications)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
set(SERVICE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cutefish-services)
qt6_add_dbus_adaptor(ADAPTOR ${SERVICE_DIR}/locale/com.cutefish.Services.Language.xml
    ${SERVICE_DIR}/locale/language.h Language)
add_executable(language_service_test language_service_test.cpp ${SERVICE_DIR}/locale/language.cpp
    ${ADAPTOR})
target_include_directories(language_service_test PRIVATE ${SERVICE_DIR})
target_link_libraries(language_service_test PRIVATE Cutefish::Applications Qt6::Core Qt6::Gui
    Qt6::DBus Qt6::Test)
enable_testing()
add_test(NAME language_service_test COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:language_service_test>)
