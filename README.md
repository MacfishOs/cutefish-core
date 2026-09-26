# Core

System backend and start session and more.

## Compile dependencies

Install Qt 6, Wayland development libraries, wayland-protocols (>= 1.39),
Polkit, Fontconfig, FreeType, and the development tools provided by your
distribution. Core does not depend on KDE Frameworks.

The screen recorder, `cutefish-screenrecorder` in libexec, also needs FFmpeg
(libavcodec, libavformat, libavutil, libswscale), libva, libdrm and PipeWire.
The shell starts it with the output and region or the window to record and
stops it on its standard input. Frames arrive as DMA-BUFs and are converted and
encoded on the GPU through VA-API; without a VA-API driver they come in shared
memory and are encoded with x264.

The services daemon consumes `ext_idle_notifier_v1` from the compositor for
idle timeouts and resume notifications. Cutefish compositor implements this
protocol; idle detection requires a compositor that advertises it.

## Permission prompts

`cutefish-services` exports `com.cutefish.Services.Permissions` at
`/com/cutefish/Services/Permissions` so that portals and other non-drawing
services can ask the user for a permission with one dialog style:

- `Prompt(token s, kind s, app_id s, parent_window s, options a{sv})`
  → `(response u, results a{sv})`. The reply waits for the user: `0` allowed,
  `1` denied or dismissed, `2` the prompt could not be shown. `results` carries
  the kind's own choice.
  `parent_window` is a portal window identifier; `wayland:<handle>` parents the
  dialog through xdg-foreign and makes it modal.
- `Dismiss(token s)` closes a prompt; only the connection that opened it may.
  A caller that leaves the bus takes its prompts with it.

The dialog offers Cancel and the kind's accept button; nothing is remembered
here, an application that should not ask again is the caller's job (the
screen cast portal hands out restore tokens). Kinds are listed in
`cutefish-services/permissions/permissionprompt.cpp`; each names a QML content
component, which provides the title, description and accept label. `screen-recording` takes `types` (the portal's source-type bitmask),
`monitors` (`aa{sv}` with `name`, `description`, `x`, `y`, `width`, `height`)
and `windows` (`aa{sv}` with `identifier`, `title`, `app_id`), shows whichever
are offered, and answers with `monitors` (`as`) or `windows` (`as`).
The shell keeps these windows out of the task list and leaves the calling
application's identity in the status bar, as it does for the file chooser.

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
