project(cutefish-appruntime)
set(TARGET cutefish-appruntime)

set(SOURCES
    applicationscope.cpp
    applicationscope.h
    appruntime.cpp
    booster.cpp
    booster.h
    chromiumswitches.cpp
    chromiumswitches.h
    main.cpp
)

qt6_add_dbus_adaptor(DBUS_SOURCES
                     com.cutefish.AppRuntime.xml
                     appruntime.h AppRuntime
                     appruntimeadaptor AppRuntimeAdaptor)
set_source_files_properties(${DBUS_SOURCES} PROPERTIES SKIP_AUTOGEN ON)

add_executable(${TARGET} ${SOURCES} ${DBUS_SOURCES})
target_compile_definitions(${TARGET} PRIVATE
    BOOSTER_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/cutefish-booster"
    INSTALL_BINDIR="${CMAKE_INSTALL_FULL_BINDIR}"
    APPLICATION_MODULE_DIR="${CMAKE_INSTALL_PREFIX}/${CUTEFISH_APPLICATION_MODULE_DIR}"
)
target_link_libraries(${TARGET}
    Cutefish::Application
    Qt6::Core
    Qt6::DBus
)

install(TARGETS ${TARGET} DESTINATION ${CMAKE_INSTALL_BINDIR})
