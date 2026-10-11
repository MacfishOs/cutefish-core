add_executable(cutefish-booster
    main.cpp
    protocol.h
    preload.qrc
)
target_link_libraries(cutefish-booster
    Cutefish::Application
    Qt6::CorePrivate
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)

install(TARGETS cutefish-booster DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
