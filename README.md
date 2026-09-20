# Core

System backend and start session and more.

## Compile dependencies

Install Qt 6, Wayland development libraries, wayland-protocols (>= 1.39),
Polkit, Fontconfig, FreeType, and the development tools provided by your
distribution. Core does not depend on KDE Frameworks.

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
  `always` (`b`, the user chose "Always Allow") plus the kind's own choice.
  `parent_window` is a portal window identifier; `wayland:<handle>` parents the
  dialog through xdg-foreign and makes it modal.
- `Dismiss(token s)` closes a prompt; only the connection that opened it may.
  A caller that leaves the bus takes its prompts with it.

The dialog offers Deny, Allow Once and, when `app_id` is not empty, Always
Allow; remembering a grant is the caller's job. Kinds are listed in
`cutefish-services/permissions/permissionprompt.cpp`; each names a QML content
component. `screen-recording` takes `monitors` (`aa{sv}` with `name`,
`description`, `x`, `y`, `width`, `height`) and answers with `monitors` (`as`).
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
