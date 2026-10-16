#include "shellprocess.h"

#include <fcntl.h>
#include <unistd.h>

#include <QDebug>
#include <QProcessEnvironment>

#include "shellconnection.h"

ShellProcess::ShellProcess(QObject* parent)
    : QProcess(parent)
{
    setProcessChannelMode(QProcess::ForwardedChannels);
    connect(this, &QProcess::started, this, [this] {
        qInfo() << "Session started" << program() << "PID" << processId();
    });
    connect(this, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        qWarning() << program() << errorString();
    });
    connect(this, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        qInfo() << program() << "exited:" << code << status;
    });
}

ShellProcess::~ShellProcess()
{
    stop();
}

void ShellProcess::startForSession(const QString& program, const QString& socket,
                                   ShellConnection* connection)
{
    if (state() != QProcess::NotRunning)
        return;

    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("WAYLAND_DISPLAY"), socket);
    environment.remove(QStringLiteral("WAYLAND_SOCKET"));

    QStringList arguments {QStringLiteral("--daemon")};
    int shellFd = -1;
    if (connection)
        shellFd = connection->createShellConnection();

    if (shellFd >= 0) {
        arguments << QStringLiteral("--desktop-socket") << QString::number(shellFd);
        // The fd is close-on-exec here so it cannot leak into anything else the
        // session starts; the child clears the flag on its own copy just before
        // exec, which is the only process that may have it.
        setChildProcessModifier([shellFd] {
            fcntl(shellFd, F_SETFD, 0);
        });
    } else {
        setChildProcessModifier({});
        qWarning() << "Starting" << program << "without a privileged connection; "
                      "there will be no desktop";
    }

    setProcessEnvironment(environment);
    setProgram(program);
    setArguments(arguments);
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
