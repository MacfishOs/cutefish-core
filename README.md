# Core

The Cutefish session, system services and their helpers.

## Dependencies

Qt 6, Wayland development libraries, wayland-protocols (>= 1.39), Polkit,
Fontconfig and FreeType. The screen recorder also needs FFmpeg, libva, libdrm
and PipeWire.

## Build

```shell
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr ..
make
```

## Install

```shell
sudo make install
```

Install `cutefish-compositor` and `cutefish-shell` as well, then select
`Cutefish (Wayland)` in the display manager.

## License

This project has been licensed by GPLv3.
