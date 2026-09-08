#include "compositorprocess.h"

#include <fcntl.h>
#include <unistd.h>

CompositorProcess::CompositorProcess(QObject* parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::ForwardedChannels);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("Compositor startup timed out"));
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_stopping && error == QProcess::FailedToStart)
            fail(m_process.errorString());
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        closeReadyPipe();
        if (m_stopping || m_failed)
            return;
        if (!m_ready)
            fail(QStringLiteral("Compositor exited before becoming ready"));
        else
            emit exited(status == QProcess::NormalExit ? code : 1);
    });
}

CompositorProcess::~CompositorProcess()
{
    stop();
}

void CompositorProcess::closeReadyPipe()
{
    m_timeout.stop();
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_readFd >= 0) {
        close(m_readFd);
        m_readFd = -1;
    }
}

void CompositorProcess::fail(const QString& reason)
{
    if (m_failed || m_stopping)
        return;
    m_failed = true;
    stop();
    emit failed(reason);
}

void CompositorProcess::start(const QString& program, const QString& socket,
                              const QString& controlSocket, int timeoutMs,
                              const QString& xwaylandReadyFile)
{
    if (m_process.state() != QProcess::NotRunning || m_readFd >= 0)
        return;
    m_ready = false;
    m_stopping = false;
    m_failed = false;
    int fds[2];
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
        fail(QStringLiteral("Cannot create compositor startup pipe"));
        return;
    }
    m_readFd = fds[0];
    const int writeFd = fds[1];
    m_process.setProgram(program);
    QStringList arguments {QStringLiteral("--socket"), socket, QStringLiteral("--ready-fd"),
                           QString::number(writeFd)};
    if (!controlSocket.isEmpty())
        arguments << QStringLiteral("--shell-control-socket") << controlSocket;
    if (!xwaylandReadyFile.isEmpty())
        arguments << QStringLiteral("--xwayland-ready-file") << xwaylandReadyFile;
    m_process.setArguments(arguments);
    m_process.setChildProcessModifier([readFd = fds[0], writeFd] {
        close(readFd);
        fcntl(writeFd, F_SETFD, 0);
    });
    m_notifier = new QSocketNotifier(m_readFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] {
        char marker = 0;
        const auto count = read(m_readFd, &marker, 1);
        if (count < 0)
            return;
        if (count != 1 || marker != 'R') {
            fail(QStringLiteral("Compositor did not confirm startup"));
            return;
        }
        closeReadyPipe();
        m_ready = true;
        emit ready();
    });
    m_process.start();
    const bool started = m_process.waitForStarted(5000);
    close(writeFd);
    if (!started) {
        fail(m_process.errorString());
        return;
    }
    if (!m_ready && !m_failed && !m_stopping)
        m_timeout.start(timeoutMs);
}

void CompositorProcess::stop()
{
    m_stopping = true;
    closeReadyPipe();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(3000)) {
            m_process.kill();
            m_process.waitForFinished(1000);
        }
    }
    m_ready = false;
}
