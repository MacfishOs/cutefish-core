pkg_check_modules(CrashSystemd REQUIRED IMPORTED_TARGET libsystemd)

add_library(cutefish-crash STATIC
    coredump.cpp
    coredump.h
    coredumpjournal.cpp
    coredumpjournal.h
    crashdialog.cpp
    crashdialog.h
    crashmanager.cpp
    crashmanager.h
    crash.qrc
)
target_include_directories(cutefish-crash PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cutefish-crash
    PUBLIC Qt6::Core Qt6::DBus Qt6::Gui Qt6::Qml Qt6::Quick
    PRIVATE Cutefish::Application PkgConfig::CrashSystemd)
