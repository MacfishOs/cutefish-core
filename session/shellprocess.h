#pragma once

#include <functional>
#include <string>

#include "base/process.h"

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
class ShellProcess final
{
public:
    ShellProcess();
    ~ShellProcess();

    //! Runs `program --daemon [--desktop-socket <fd>]`. `connection` may be
    //! null, in which case the daemon runs without a desktop. False when it
    //! is running already or cannot be started.
    bool startForSession(const std::string& program, const std::string& socket,
                         ShellConnection* connection);
    void stop();

    bool isRunning() const { return m_process.isRunning(); }

    std::function<void(int code, bool crashed)> onFinished;

private:
    cutefish::base::Process m_process;
    std::string m_program;
};
