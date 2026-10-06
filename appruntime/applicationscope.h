#pragma once

#include <QString>

// Transient systemd scopes for launched applications, named
// app-cutefish-<AppID>-<pid>.scope as the desktop entry specification asks,
// which is how xdg-desktop-portal learns the app id of a host application.
namespace ApplicationScope {

// The unit name, with the app id escaped the way systemd escapes unit names.
QString unitName(const QString &appId, uint pid);

// Moves the process into its own scope under the user manager. Failure is
// logged and otherwise ignored: the application runs either way.
void adopt(const QString &appId, uint pid);

// Sends SIGKILL to every process left in the scope. A scope that already went
// with its last process is no failure.
void killAll(const QString &unitName);

} // namespace ApplicationScope
