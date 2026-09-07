#pragma once

#include <QProcess>

class ShellConnection;

/**
 * The cutefish-shell process.
 *
 * The shell is started on a privileged Wayland connection the session obtained
 * from the compositor: the socketpair end is passed as WAYLAND_SOCKET, which
 * Qt Wayland uses in preference to WAYLAND_DISPLAY. WAYLAND_DISPLAY is set as
 * well, so everything the shell launches connects to the ordinary public
 * socket instead.
 */
class ShellProcess final : public QProcess
{
public:
    explicit ShellProcess(QObject *parent = nullptr);
    ~ShellProcess() override;

    //! `connection` may be null, in which case the shell falls back to the
    //! public socket and simply gets no private globals.
    void startForSession(const QString &program, const QString &socket,
                         ShellConnection *connection);
    void stop();
};
