find_package(PkgConfig REQUIRED)
pkg_check_modules(PermissionsWayland REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(PermissionsProtocols REQUIRED wayland-protocols>=1.39)
pkg_get_variable(PERMISSIONS_PROTOCOL_DIR wayland-protocols pkgdatadir)
pkg_get_variable(PERMISSIONS_SCANNER wayland-scanner wayland_scanner)

# The requesting window belongs to another process, so the prompt is parented
# through xdg-foreign.
set(PERMISSIONS_WAYLAND_SOURCES)
foreach(protocol xdg-shell:stable/xdg-shell
                 xdg-dialog-v1:staging/xdg-dialog
                 xdg-foreign-unstable-v2:unstable/xdg-foreign)
    string(REPLACE ":" ";" parts ${protocol})
    list(GET parts 0 name)
    list(GET parts 1 directory)
    set(xml ${PERMISSIONS_PROTOCOL_DIR}/${directory}/${name}.xml)
    set(header ${CMAKE_CURRENT_BINARY_DIR}/${name}-client-protocol.h)
    set(code ${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.c)
    add_custom_command(OUTPUT ${header} ${code}
        COMMAND ${PERMISSIONS_SCANNER} client-header ${xml} ${header}
        COMMAND ${PERMISSIONS_SCANNER} private-code ${xml} ${code}
        DEPENDS ${xml} VERBATIM)
    list(APPEND PERMISSIONS_WAYLAND_SOURCES ${header} ${code})
endforeach()

add_library(cutefish-permissions STATIC
    permissionmanager.cpp
    permissionmanager.h
    permissionprompt.cpp
    permissionprompt.h
    permissions.qrc
    waylandparent.cpp
    waylandparent.h
    ${PERMISSIONS_WAYLAND_SOURCES}
)
target_include_directories(cutefish-permissions PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(cutefish-permissions PRIVATE CUTEFISH_PERMISSIONS_WAYLAND)
target_link_libraries(cutefish-permissions
    PUBLIC Qt6::Core Qt6::DBus Qt6::Gui Qt6::Qml Qt6::Quick
    PRIVATE Qt6::QuickControls2 Qt6::GuiPrivate Cutefish::Applications PkgConfig::PermissionsWayland)
