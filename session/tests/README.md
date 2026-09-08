Session Xwayland integration
===========================

The session creates a private readiness directory, passes its JSON path to the
compositor, and waits asynchronously for authenticated X11 readiness after native
Wayland readiness. The 12-second timeout continues native desktop startup.
DISPLAY and XAUTHORITY are validated and inherited by the Shell, application
runtime and services. QT_QPA_PLATFORM remains wayland for the Shell itself.
D-Bus/systemd activation is updated before desktop startup with a three-second
subprocess deadline; failures are logged without ending the native session.

The readiness directory is unique per session. File removal clears the session
environment and activation values. Existing processes retain their inherited
environment; no automatic Xwayland restart or process environment rewriting is
implemented. Previously running user services need restarting to receive new
values. Install both the compositor and session changes, then log out and back in.

Validation:

```sh
cmake -S core/session/tests -B /tmp/cutefish-session-xwayland-tests
cmake --build /tmp/cutefish-session-xwayland-tests -j4
CUTEFISH_TEST_COMPOSITOR=/tmp/cutefish-dev-build-1000/cutefish-compositor/cutefish-compositor \
  ctest --test-dir /tmp/cutefish-session-xwayland-tests --output-on-failure
cmake --build /tmp/cutefish-dev-build-1000/core --target cutefish-session -j4
```

Baseline: the two existing CTest executables passed with private socket access.
Updated validation: all three CTest executables passed, including real Xwayland
readiness and an authenticated xdpyinfo child connection. Tests cover both
readiness arrival orders, stale environment removal, malformed data, unsafe
authority permissions, timeout, removal, compositor argument forwarding, Shell
environment inheritance and native survival with Xwayland disabled.

The session Release build passes (existing QVariant::type deprecation warning).
No system installation or active-session restart was performed. No live desktop,
keyboard viewer visual behavior, or actual user-systemd activation environment
was tested; changing the active user's service environment is deliberately avoided.

Autostart follow-up
------------------

Autostart selection now follows directory precedence by desktop filename before
filtering: a user Hidden=true entry masks the system entry. OnlyShowIn and
NotShowIn are matched against colon-separated XDG_CURRENT_DESKTOP values;
TryExec checks executable availability. NoDisplay does not disable autostart.
Semicolon-separated lists are read without QSettings INI comment truncation.
The existing Exec argument/field-code launch path is retained; this change is
not a complete Desktop Entry execution implementation.

Reference: https://specifications.freedesktop.org/autostart/latest/

The session build and all four CTest executables passed after the change,
including the real Xwayland environment test. Autostart tests cover user
suppression, duplicate filenames, filtered overrides, desktop lists, NoDisplay,
and executable checks. All session C++ sources and headers were formatted with
clang-format-19 using cutefish-compositor/.clang-format, including tests.
No user autostart configuration was changed and no system binary was installed.
