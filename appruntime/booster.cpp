// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "booster.h"
#include "appruntime.h"
#include "booster/protocol.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSocketNotifier>
#include <QStandardPaths>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

// The session is busy with its own components at first.
static const int kFirstSpawnDelay = 5000;
// Leaves the cores to the application that has just started.
static const int kSpawnDelay = 1500;
static const int kReplyTimeout = 1000;
// A booster that dies before it is ready this often in a row is not retried.
static const int kMaxFailures = 3;
// Room for the launched application's command line, which overwrites it.
static const int kCommandLineRoom = 1024;

Booster::Booster(QObject *parent)
    : QObject(parent)
{
    m_spawnTimer.setSingleShot(true);
    connect(&m_spawnTimer, &QTimer::timeout, this, &Booster::spawn);
    m_spawnTimer.start(kFirstSpawnDelay);
}

Booster::~Booster()
{
    discard();
}

uint Booster::launch(const QStringList &command, const QString &workingDirectory,
                     const QProcessEnvironment &environment, const QString &activationToken)
{
    if (!m_ready)
        return 0;

    QString executable = command.first();
    if (!QFileInfo(executable).isAbsolute())
        executable = QStandardPaths::findExecutable(executable);
    executable = QFileInfo(executable).canonicalFilePath();

    const QString module = moduleFor(executable);
    if (module.isEmpty())
        return 0;

    // The language or the proxy changed since the booster started.
    if (environment != m_environment) {
        discard();
        m_spawnTimer.start(kSpawnDelay);
        return 0;
    }

    BoosterProtocol::LaunchRequest request;
    request.module = module;
    request.executable = executable;
    request.arguments = command;
    request.workingDirectory = workingDirectory;
    request.activationToken = activationToken;

    m_notifier->setEnabled(false);
    const QByteArray message = BoosterProtocol::encode(request);
    char reply = 0;
    if (::send(m_socket, message.constData(), message.size(), MSG_NOSIGNAL) == message.size()) {
        pollfd fd = {m_socket, POLLIN, 0};
        if (::poll(&fd, 1, kReplyTimeout) == 1 && ::recv(m_socket, &reply, 1, 0) != 1)
            reply = 0;
    }

    const uint pid = reply == BoosterProtocol::Accepted ? m_pid : 0;
    if (!pid)
        qWarning() << "AppRuntime: the booster did not take" << executable;

    // Either way this booster is done: it is the application now, or it quits
    // on the closed socket.
    discard();
    m_spawnTimer.start(kSpawnDelay);

    return pid;
}

void Booster::spawn()
{
    if (m_socket >= 0)
        return;

    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, fds) != 0) {
        qWarning() << "AppRuntime: no socket for the booster:" << ::strerror(errno);
        return;
    }
    const int child = fds[1];

    m_environment = AppRuntime::applicationEnvironment();

    QProcess process;
    process.setProgram(QStringLiteral(BOOSTER_PATH));
    process.setArguments({QStringLiteral("--socket"), QString::number(child),
                          QString(kCommandLineRoom, QLatin1Char(' '))});
    process.setProcessEnvironment(m_environment);
    process.setChildProcessModifier([child] {
        ::fcntl(child, F_SETFD, 0);
    });

    qint64 pid = 0;
    const bool started = process.startDetached(&pid);
    ::close(child);
    if (!started || pid <= 0) {
        qWarning() << "AppRuntime: failed to start the booster:" << process.errorString();
        ::close(fds[0]);
        return;
    }

    m_socket = fds[0];
    m_pid = static_cast<uint>(pid);
    m_ready = false;
    m_notifier = new QSocketNotifier(m_socket, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &Booster::readReady);
}

void Booster::discard()
{
    delete m_notifier;
    m_notifier = nullptr;

    if (m_socket >= 0)
        ::close(m_socket);
    m_socket = -1;
    m_pid = 0;
    m_ready = false;
}

void Booster::readReady()
{
    char message = 0;
    if (::recv(m_socket, &message, 1, 0) == 1 && message == BoosterProtocol::Ready) {
        m_ready = true;
        m_failures = 0;
        return;
    }

    // It hung up, so it is gone.
    const bool wasReady = m_ready;
    discard();
    if (!wasReady && ++m_failures >= kMaxFailures) {
        qWarning() << "AppRuntime: the booster keeps failing; applications start without it";
        return;
    }

    m_spawnTimer.start(kSpawnDelay);
}

// Only installed executables have a module, and a module only runs as the
// executable it was installed with.
QString Booster::moduleFor(const QString &executable)
{
    const QFileInfo info(executable);
    if (executable.isEmpty() || info.path() != QStringLiteral(INSTALL_BINDIR))
        return {};

    const QString module = QStringLiteral(APPLICATION_MODULE_DIR "/") + info.fileName()
            + QStringLiteral(".so");
    return QFileInfo::exists(module) ? module : QString();
}
