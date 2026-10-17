find_package(CutefishToolkit REQUIRED)

add_executable(cutefish-polkit-agent
    main.cpp
    polkitagentlistener.cpp
    polkitagentlistener.h
    authsession.cpp
    authsession.h
    dialog.cpp
    dialog.h
)
set_target_properties(cutefish-polkit-agent PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

target_link_libraries(cutefish-polkit-agent PRIVATE Cutefish::Toolkit)

install(TARGETS cutefish-polkit-agent
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Runtime
)
