#include "shellprocess.h"

#include <QDebug>
#include <QProcessEnvironment>

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

void ShellProcess::startForSession(const QString &program, const QString &socket)
{
    if (state() != QProcess::NotRunning)
        return;

    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("DISPLAY"));
    environment.remove(QStringLiteral("WAYLAND_SOCKET"));
    environment.insert(QStringLiteral("WAYLAND_DISPLAY"), socket);
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("wayland"));
    // The compositor currently exposes shared-memory client buffers only.
    environment.insert(QStringLiteral("QT_QUICK_BACKEND"), QStringLiteral("software"));
    setProcessEnvironment(environment);
    setProgram(program);
    setArguments({});
    start();
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
