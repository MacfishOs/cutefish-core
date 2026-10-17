#include "shellprocess.h"

#include <unistd.h>

#include "common/environment.h"
#include "journalstream.h"
#include "logs.h"
#include "shellconnection.h"

using namespace std::chrono_literals;

ShellProcess::ShellProcess()
{
    m_process.setOutputForwarded(true);
    m_process.onFinished = [this](int code, bool crashed) {
        CF_INFO(sessionLog()) << m_program << "exited:" << code << (crashed ? "(crashed)" : "(normally)");
        if (onFinished)
            onFinished(code, crashed);
    };
}

ShellProcess::~ShellProcess()
{
    stop();
}

bool ShellProcess::startForSession(const std::string& program, const std::string& socket,
                                   ShellConnection* connection)
{
    if (m_process.isRunning())
        return false;

    cutefish::core::Environment environment = cutefish::core::processEnvironment();
    environment["WAYLAND_DISPLAY"] = socket;
    environment.erase("WAYLAND_SOCKET");

    std::vector<std::string> arguments {"--daemon"};
    int shellFd = -1;
    if (connection)
        shellFd = connection->createShellConnection();

    if (shellFd >= 0) {
        // The session's copy is close-on-exec, so it cannot leak into anything
        // else the session starts; the daemon inherits it as its only extra
        // descriptor.
        arguments.push_back("--desktop-socket");
        arguments.push_back(std::to_string(cutefish::base::Process::kInheritedFd));
    } else {
        CF_WARNING(sessionLog()) << "Starting" << program
                                 << "without a privileged connection; there will be no desktop";
    }

    m_program = program;
    m_process.setInheritedFd(shellFd);
    m_process.setEnvironment(cutefish::core::environmentEntries(environment));
    const bool started = startWithOwnJournalStream(m_process, program, arguments);
    if (started)
        CF_INFO(sessionLog()) << "Session started" << program << "PID" << m_process.pid();
    else
        CF_WARNING(sessionLog()) << program << "failed to start";

    // The session's own copy is no longer needed once the child has it.
    if (shellFd >= 0)
        ::close(shellFd);
    return started;
}

void ShellProcess::stop()
{
    if (!m_process.isRunning())
        return;

    m_process.terminate();
    if (!m_process.waitForFinished(3000ms)) {
        m_process.kill();
        m_process.waitForFinished(1000ms);
    }
}
