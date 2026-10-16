#pragma once

#include <QProcess>

class ShellConnection;

/**
 * The file manager daemon, which draws the desktop.
 *
 * Its desktop window lives on a privileged Wayland connection the session
 * obtained from the compositor: the socketpair end is inherited and named on
 * the command line as `--desktop-socket <fd>`. WAYLAND_DISPLAY names the
 * ordinary public socket, which its other windows and everything it launches
 * use.
 */
class ShellProcess final : public QProcess
{
public:
    explicit ShellProcess(QObject* parent = nullptr);
    ~ShellProcess() override;

    //! Runs `program --daemon [--desktop-socket <fd>]`. `connection` may be
    //! null, in which case the daemon runs without a desktop.
    void startForSession(const QString& program, const QString& socket,
                         ShellConnection* connection);
    void stop();
};
