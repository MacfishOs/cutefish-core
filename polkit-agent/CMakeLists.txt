add_executable(cutefish-polkit-agent
    main.cpp
    polkitagentlistener.cpp
    authsession.cpp
    dialog.cpp
    qml.qrc
)

target_link_libraries(cutefish-polkit-agent
        Cutefish::Application
        Qt6::Core
        Qt6::DBus
        Qt6::Network
        Qt6::Quick
        Qt6::QuickControls2

        # FishUI
)

file(GLOB TS_FILES translations/*.ts)
qt6_add_translation(QM_FILES ${TS_FILES})
add_custom_target(polkit-agent-translations DEPENDS ${QM_FILES} SOURCES ${TS_FILES})
add_dependencies(cutefish-polkit-agent polkit-agent-translations)

install(TARGETS cutefish-polkit-agent
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Runtime
)

install(FILES
cutefish-polkit-agent.desktop
    DESTINATION "${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/autostart"
    COMPONENT Runtime
)

install(FILES ${QM_FILES} DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/cutefish-polkit-agent/translations)
