# Core

System backend and start session and more.

## Compile dependencies

Install Qt 6, Wayland development libraries, wayland-protocols (>= 1.39),
Polkit, Fontconfig, FreeType, and the development tools provided by your
distribution. Core does not depend on KDE Frameworks.

The services daemon consumes `ext_idle_notifier_v1` from the compositor for
idle timeouts and resume notifications. Cutefish compositor implements this
protocol; idle detection requires a compositor that advertises it.

## Runtime

For the Wayland session, install `cutefish-compositor` and `cutefish-shell`,
then select `Cutefish (Wayland)` from the display manager. The session entry
starts `cutefish-session`, which supervises the compositor and the separate
shell process. The services daemon provides the session's system backends.

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

## License

This project has been licensed by GPLv3.
