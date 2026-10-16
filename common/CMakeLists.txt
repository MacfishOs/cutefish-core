# What the session, the runtime and the services share: the locale and proxy
# variables every application is started with, the GSettings keyfile and the
# runtime's client.
pkg_check_modules(ICU REQUIRED IMPORTED_TARGET icu-uc)

add_library(cutefish-core-common STATIC
    detached_process.cpp
    detached_process.h
    environment.cpp
    environment.h
    gsettings_keyfile.cpp
    gsettings_keyfile.h
    language_environment.cpp
    language_environment.h
    proxy_settings.cpp
    proxy_settings.h
    runtime_client.cpp
    runtime_client.h
)
target_include_directories(cutefish-core-common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(cutefish-core-common
    PUBLIC Cutefish::ToolkitBase
    PRIVATE PkgConfig::ICU
)
