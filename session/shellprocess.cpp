#include "shellprocess.h"
#include "shellconnection.h"

#include <QDebug>
#include <QProcessEnvironment>

#include <fcntl.h>
#include <unistd.h>

ShellProcess::ShellProcess(QObject *parent)
    : QProcess(parent)
{
    setProcessChannelMode(QProcess::ForwardedChannels);
    connect(this, &QProcess::started, this, [this] {
        qInfo() << "Session started cutefish-shell, PID" << processId();
    });
    connect(this, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        qWarning() << "cutefish-shell:" << errorString();
    });
    connect(this, &QProcess::finished, this, [](int code, QProcess::ExitStatus status) {
        qInfo() << "cutefish-shell exited:" << code << status;
    });
}

ShellProcess::~ShellProcess()
{
    stop();
}

void ShellProcess::startForSession(const QString &program, const QString &socket,
                                   ShellConnection *connection)
{
    if (state() != QProcess::NotRunning)
        return;

    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("DISPLAY"));
    environment.insert(QStringLiteral("WAYLAND_DISPLAY"), socket);
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("wayland"));

    int shellFd = -1;
    if (connection)
        shellFd = connection->createShellConnection();

    if (shellFd >= 0) {
        environment.insert(QStringLiteral("WAYLAND_SOCKET"), QString::number(shellFd));
        // The fd is close-on-exec here so it cannot leak into anything else the
        // session starts; the child clears the flag on its own copy just before
        // exec, which is the only process that may have it.
        setChildProcessModifier([shellFd] { fcntl(shellFd, F_SETFD, 0); });
    } else {
        environment.remove(QStringLiteral("WAYLAND_SOCKET"));
        setChildProcessModifier({});
        qWarning() << "Starting cutefish-shell without a privileged connection; "
                      "the private shell protocols will be unavailable";
    }

    setProcessEnvironment(environment);
    setProgram(program);
    setArguments({});
    start();
    waitForStarted(5000);

    // The session's own copy is no longer needed once the child has it.
    if (shellFd >= 0)
        ::close(shellFd);
}

void ShellProcess::stop()
{
    if (state() == QProcess::NotRunning)
        return;

    terminate();
    if (!waitForFinished(3000)) {
        kill();
        waitForFinished(1000);
    }
}
