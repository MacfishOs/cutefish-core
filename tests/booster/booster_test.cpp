// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "booster/protocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

class BoosterTest : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();
    void launchGivesTheApplicationItsIdentity();
    void moduleWithoutEntryIsRefused();
    void idleBoosterQuitsWithTheRuntime();

private:
    // Starts the booster and waits until it has preloaded.
    bool start();
    char receive(int timeout);
    bool send(const BoosterProtocol::LaunchRequest &request);

    QProcess m_booster;
    int m_socket = -1;
};

void BoosterTest::cleanup()
{
    if (m_socket >= 0)
        ::close(m_socket);
    m_socket = -1;

    if (m_booster.state() != QProcess::NotRunning) {
        m_booster.kill();
        m_booster.waitForFinished();
    }
}

bool BoosterTest::start()
{
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, fds) != 0)
        return false;
    const int child = fds[1];

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    environment.remove(QStringLiteral("XDG_ACTIVATION_TOKEN"));
    m_booster.setProcessEnvironment(environment);
    m_booster.setProcessChannelMode(QProcess::SeparateChannels);
    m_booster.setChildProcessModifier([child] {
        ::fcntl(child, F_SETFD, 0);
    });
    m_booster.start(QStringLiteral(BOOSTER_PATH),
                    {QStringLiteral("--socket"), QString::number(child), QString(256, QLatin1Char(' '))});
    const bool started = m_booster.waitForStarted();
    ::close(child);
    m_socket = fds[0];

    return started && receive(30000) == BoosterProtocol::Ready;
}

char BoosterTest::receive(int timeout)
{
    pollfd fd = {m_socket, POLLIN, 0};
    char message = 0;
    if (::poll(&fd, 1, timeout) != 1 || ::recv(m_socket, &message, 1, 0) != 1)
        return 0;

    return message;
}

bool BoosterTest::send(const BoosterProtocol::LaunchRequest &request)
{
    const QByteArray message = BoosterProtocol::encode(request);
    return ::send(m_socket, message.constData(), message.size(), MSG_NOSIGNAL) == message.size();
}

void BoosterTest::launchGivesTheApplicationItsIdentity()
{
    QVERIFY(start());

    QTemporaryDir workingDirectory;
    QVERIFY(workingDirectory.isValid());

    BoosterProtocol::LaunchRequest request;
    request.module = QStringLiteral(APPLICATION_MODULE);
    request.executable = QStringLiteral("/usr/bin/cutefish-example");
    request.arguments = {QStringLiteral("cutefish-example"), QStringLiteral("--page"),
                         QStringLiteral("two words")};
    request.workingDirectory = workingDirectory.path();
    request.activationToken = QStringLiteral("token-1");
    QVERIFY(send(request));
    QCOMPARE(receive(5000), BoosterProtocol::Accepted);

    QVERIFY(m_booster.waitForFinished(10000));
    QCOMPARE(m_booster.exitStatus(), QProcess::NormalExit);
    QCOMPARE(m_booster.exitCode(), 7);

    const QJsonObject report = QJsonDocument::fromJson(m_booster.readAllStandardOutput()).object();
    const QJsonArray arguments = QJsonArray::fromStringList(request.arguments);
    QVERIFY(report.value(QStringLiteral("hasApplication")).toBool());
    QCOMPARE(report.value(QStringLiteral("argv")).toArray(), arguments);
    QCOMPARE(report.value(QStringLiteral("arguments")).toArray(), arguments);
    QCOMPARE(report.value(QStringLiteral("applicationFilePath")).toString(), request.executable);
    QCOMPARE(report.value(QStringLiteral("applicationDirPath")).toString(), QStringLiteral("/usr/bin"));
    QCOMPARE(report.value(QStringLiteral("applicationName")).toString(), QStringLiteral("cutefish-example"));
    QCOMPARE(report.value(QStringLiteral("currentPath")).toString(), workingDirectory.path());
    QCOMPARE(report.value(QStringLiteral("activationToken")).toString(), request.activationToken);
    QCOMPARE(report.value(QStringLiteral("cmdline")).toString(),
             QStringLiteral("cutefish-example --page two words"));
    QCOMPARE(report.value(QStringLiteral("comm")).toString(), QStringLiteral("cutefish-exampl"));
}

void BoosterTest::moduleWithoutEntryIsRefused()
{
    QVERIFY(start());

    BoosterProtocol::LaunchRequest request;
    request.module = QStringLiteral(NO_ENTRY_MODULE);
    request.executable = QStringLiteral("/usr/bin/cutefish-example");
    request.arguments = {QStringLiteral("cutefish-example")};
    QVERIFY(send(request));
    QCOMPARE(receive(5000), BoosterProtocol::Refused);

    QVERIFY(m_booster.waitForFinished(10000));
    QCOMPARE(m_booster.exitStatus(), QProcess::NormalExit);
    QVERIFY(m_booster.readAllStandardOutput().isEmpty());
}

void BoosterTest::idleBoosterQuitsWithTheRuntime()
{
    QVERIFY(start());

    ::close(m_socket);
    m_socket = -1;

    QVERIFY(m_booster.waitForFinished(10000));
    QCOMPARE(m_booster.exitStatus(), QProcess::NormalExit);
}

QTEST_GUILESS_MAIN(BoosterTest)

#include "booster_test.moc"
