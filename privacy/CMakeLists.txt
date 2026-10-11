project(cutefish-privacy)
set(TARGET cutefish-privacy)

set(SOURCES
    captureswitches.cpp
    captureswitches.h
    main.cpp
    privacyservice.cpp
    privacyservice.h
    usbcameras.cpp
    usbcameras.h
)

qt6_add_dbus_adaptor(DBUS_SOURCES
                     com.cutefish.Privacy.xml
                     privacyservice.h PrivacyService
                     privacyadaptor PrivacyAdaptor)
set_source_files_properties(${DBUS_SOURCES} PROPERTIES SKIP_AUTOGEN ON)

add_executable(${TARGET} ${SOURCES} ${DBUS_SOURCES})
target_link_libraries(${TARGET}
    Qt6::Core
    Qt6::DBus
)

configure_file(com.cutefish.Privacy.service.in com.cutefish.Privacy.service @ONLY)
configure_file(cutefish-privacy.service.in cutefish-privacy.service @ONLY)

install(TARGETS ${TARGET} DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(FILES com.cutefish.Privacy.conf DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/dbus-1/system.d)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/com.cutefish.Privacy.service
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/dbus-1/system-services)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cutefish-privacy.service DESTINATION lib/systemd/system)
install(FILES com.cutefish.privacy.policy DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/polkit-1/actions)
install(FILES 60-cutefish-privacy.rules DESTINATION lib/udev/rules.d)
