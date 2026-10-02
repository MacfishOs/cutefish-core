cmake_minimum_required(VERSION 3.22)
project(wayland_parent_test LANGUAGES C CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Gui Test)
if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WaylandServer REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(WaylandProtocols REQUIRED wayland-protocols>=1.39)
pkg_get_variable(PROTOCOL_DIR wayland-protocols pkgdatadir)
pkg_get_variable(SCANNER wayland-scanner wayland_scanner)
set(PERMISSIONS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cutefish-services/permissions)

set(PROTOCOL_SOURCES)
foreach(protocol xdg-shell:stable/xdg-shell
                 xdg-dialog-v1:staging/xdg-dialog
                 xdg-foreign-unstable-v2:unstable/xdg-foreign)
    string(REPLACE ":" ";" parts ${protocol})
    list(GET parts 0 name)
    list(GET parts 1 directory)
    set(xml ${PROTOCOL_DIR}/${directory}/${name}.xml)
    set(client_header ${CMAKE_CURRENT_BINARY_DIR}/${name}-client-protocol.h)
    set(server_header ${CMAKE_CURRENT_BINARY_DIR}/${name}-server-protocol.h)
    set(code ${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.c)
    add_custom_command(OUTPUT ${client_header} ${server_header} ${code}
        COMMAND ${SCANNER} client-header ${xml} ${client_header}
        COMMAND ${SCANNER} server-header ${xml} ${server_header}
        COMMAND ${SCANNER} private-code ${xml} ${code}
        DEPENDS ${xml} VERBATIM)
    list(APPEND PROTOCOL_SOURCES ${client_header} ${server_header} ${code})
endforeach()

# The prompt's parent objects, made through the Qt Wayland platform against a
# compositor running in the test.
add_executable(wayland_parent_test wayland_parent_test.cpp ${PERMISSIONS_DIR}/waylandparent.cpp
    ${PROTOCOL_SOURCES})
target_include_directories(wayland_parent_test PRIVATE ${PERMISSIONS_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(wayland_parent_test PRIVATE Qt6::Gui Qt6::GuiPrivate Qt6::Test
    PkgConfig::WaylandClient PkgConfig::WaylandServer)
enable_testing()
add_test(NAME wayland_parent_test COMMAND wayland_parent_test)
