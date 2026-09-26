# cutefish-screenrecorder: the shell's screen recordings, captured through the
# compositor and encoded here, so neither the shell nor the compositor ever
# encodes. The compositor allows it to capture by its installed path. Frames
# come as DMA-BUFs and stay on the GPU through VA-API where it can.
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(FFmpeg REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswscale)
pkg_check_modules(LibVA REQUIRED IMPORTED_TARGET libva)
pkg_check_modules(Libdrm REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(PipeWire REQUIRED IMPORTED_TARGET libpipewire-0.3)

add_executable(cutefish-screenrecorder
    main.cpp
    audiocapture.cpp
    encoder.cpp
    recorder.cpp
)
target_link_libraries(cutefish-screenrecorder PRIVATE
    Cutefish::Capture
    Qt6::Core
    Qt6::Gui
    PkgConfig::WaylandClient
    PkgConfig::FFmpeg
    PkgConfig::LibVA
    PkgConfig::Libdrm
    PkgConfig::PipeWire
)

install(TARGETS cutefish-screenrecorder
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}
    COMPONENT Runtime
)
