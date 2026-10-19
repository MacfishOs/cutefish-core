# Core

The Cutefish session, system services and their helpers.

## Dependencies

cutefish-toolkit, the display client of cutefish-framework, libsystemd, ICU,
Fontconfig, Wayland development libraries and wayland-protocols (>= 1.39).
The screen recorder also needs FFmpeg, libva, libdrm, GBM and PipeWire; the
tests need GoogleTest.

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

Install `cutefish-compositor` and `cutefish-filemanager` as well, then select
`Cutefish (Wayland)` in the display manager.

## License

BSD-3-Clause; see [LICENSE](LICENSE).
