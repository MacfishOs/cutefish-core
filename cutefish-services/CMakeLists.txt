project(cutefish-services)
set(TARGET cutefish-services)


file(GLOB_RECURSE SOURCES
  "*.cpp"
  "*.h"
)

list(FILTER SOURCES EXCLUDE REGEX "/clipboard/")
list(FILTER SOURCES EXCLUDE REGEX "/idle/")
list(FILTER SOURCES EXCLUDE REGEX "/permissions/")
add_subdirectory(clipboard)
add_subdirectory(idle)
add_subdirectory(permissions)

set(HEADERS "")
set(FORMS "")
set(RESOURCES "")

qt6_add_dbus_adaptor(DBUS_SOURCES
                     brightness/com.cutefish.Services.Brightness.xml
                     brightness/brightnessmanager.h BrightnessManager)
qt6_add_dbus_adaptor(DBUS_SOURCES
                     appearance/com.cutefish.Appearance.xml
                     appearance/thememanager.h ThemeManager)
qt6_add_dbus_adaptor(DBUS_SOURCES
                     battery/com.cutefish.Battery.xml
                     battery/battery.h Battery)
qt6_add_dbus_adaptor(DBUS_SOURCES
                     locale/com.cutefish.Services.Language.xml
                     locale/language.h Language)
qt6_add_dbus_adaptor(DBUS_SOURCES
                     power/com.cutefish.Power.xml
                     power/powermanager.h PowerManager)
set_source_files_properties(${DBUS_SOURCES} PROPERTIES SKIP_AUTOGEN ON)

add_executable(${TARGET} ${SOURCES} ${DBUS_SOURCES} ${HEADERS} ${FORMS} ${RESOURCES})

target_compile_definitions(${TARGET} PRIVATE
        CUTEFISH_GTK_CSS_DIR="${CMAKE_INSTALL_FULL_DATAROOTDIR}/${TARGET}/gtk"
)

target_link_libraries(${TARGET}
        Cutefish::Application
        cutefish-clipboard-backend
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::DBus
        Qt6::Xml
        cutefish-idle-backend
        cutefish-permissions
)

file(GLOB TS_FILES translations/*.ts)
qt6_add_translation(QM_FILES ${TS_FILES})
add_custom_target(translations DEPENDS ${QM_FILES} SOURCES ${TS_FILES})
add_dependencies(${TARGET} translations)

install(TARGETS ${TARGET} DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${QM_FILES} DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${TARGET}/translations)
install(FILES appearance/gtk/gtk-3.0.css appearance/gtk/gtk-4.0.css
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${TARGET}/gtk)
install(FILES brightness/60-cutefish-ddc.rules DESTINATION lib/udev/rules.d)
install(FILES brightness/cutefish-ddc.conf DESTINATION lib/modules-load.d)
