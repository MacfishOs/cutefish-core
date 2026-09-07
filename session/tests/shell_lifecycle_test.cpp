#include "shellconnection.h"
#include "shellprocess.h"

#include <QCoreApplication>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{
QString controlSocketPath()
{
    return QStringLiteral("%1/cutefish-shell-lifecycle-test.control")
        .arg(QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR").isEmpty()
                                        ? QByteArray("/tmp")
                                        : qgetenv("XDG_RUNTIME_DIR")));
}

// A stand-in for the compositor's control channel: accepts one connection and
// collects the descriptors sent over it.
class FakeCompositor
{
public:
    bool listen(const QString &path)
    {
        m_path = path;
        ::unlink(path.toUtf8().constData());

        sockaddr_un address {};
        address.sun_family = AF_UNIX;
        const QByteArray native = path.toUtf8();
        std::memcpy(address.sun_path, native.constData(), native.size());

        m_listenFd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (m_listenFd < 0)
            return false;
        if (bind(m_listenFd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
            return false;
        return ::listen(m_listenFd, 4) == 0;
    }

    bool accept()
    {
        m_clientFd = accept4(m_listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        return m_clientFd >= 0;
    }

    //! Blocks for the next descriptor, or -1.
    int receiveFd()
    {
        char payload = 0;
        iovec iov {};
        iov.iov_base = &payload;
        iov.iov_len = 1;

        union {
            char buffer[CMSG_SPACE(sizeof(int))];
            cmsghdr alignment;
        } control {};

        msghdr message {};
        message.msg_iov = &iov;
        message.msg_iovlen = 1;
        message.msg_control = control.buffer;
        message.msg_controllen = sizeof(control.buffer);

        if (recvmsg(m_clientFd, &message, MSG_CMSG_CLOEXEC) <= 0)
            return -1;

        for (cmsghdr *header = CMSG_FIRSTHDR(&message); header;
             header = CMSG_NXTHDR(&message, header)) {
            if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS) {
                int fd = -1;
                std::memcpy(&fd, CMSG_DATA(header), sizeof(fd));
                return fd;
            }
        }
        return -1;
    }

    ~FakeCompositor()
    {
        if (m_clientFd >= 0)
            ::close(m_clientFd);
        if (m_listenFd >= 0)
            ::close(m_listenFd);
        if (!m_path.isEmpty())
            ::unlink(m_path.toUtf8().constData());
    }

private:
    QString m_path;
    int m_listenFd = -1;
    int m_clientFd = -1;
};
}

class ShellLifecycleTest : public QObject
{
    Q_OBJECT
private slots:
    void environmentAndSingleStart()
    {
        qputenv("SHELL_TEST_CHILD", "1");
        ShellProcess process;
        QSignalSpy started(&process, &QProcess::started);
        QSignalSpy finished(&process, &QProcess::finished);
        process.startForSession(QCoreApplication::applicationFilePath(), "shell-test-socket",
                                nullptr);
        process.startForSession("/does-not-exist/duplicate-shell", "wrong-socket", nullptr);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(started.count(), 1);
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
    }

    void stopAndMissingExecutable()
    {
        qputenv("SHELL_TEST_CHILD", "hang");
        ShellProcess process;
        process.startForSession(QCoreApplication::applicationFilePath(), "shell-test-socket",
                                nullptr);
        QVERIFY(process.waitForStarted());
        process.stop();
        QCOMPARE(process.state(), QProcess::NotRunning);
        process.stop();

        QSignalSpy failed(&process, &QProcess::errorOccurred);
        process.startForSession("/does-not-exist/cutefish-shell", "shell-test-socket", nullptr);
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(process.error(), QProcess::FailedToStart);
    }

    // The socketpair the session creates really reaches both sides: the
    // compositor gets one end over SCM_RIGHTS, the shell the other.
    void theCompositorReceivesTheShellConnection()
    {
        FakeCompositor compositor;
        const QString path = controlSocketPath();
        QVERIFY(compositor.listen(path));

        ShellConnection connection;
        QVERIFY(connection.connectToCompositor(path, 2000));
        QVERIFY(compositor.accept());

        const int shellEnd = connection.createShellConnection();
        QVERIFY(shellEnd >= 0);

        const int compositorEnd = compositor.receiveFd();
        QVERIFY(compositorEnd >= 0);

        // The two descriptors are the two ends of one socket.
        QCOMPARE(::write(shellEnd, "x", 1), ssize_t(1));
        char received = 0;
        QCOMPARE(::read(compositorEnd, &received, 1), ssize_t(1));
        QCOMPARE(received, 'x');

        ::close(shellEnd);
        ::close(compositorEnd);
    }

    // The shell is started with a usable WAYLAND_SOCKET and the public
    // WAYLAND_DISPLAY, and neither the descriptor nor the variable reaches
    // anything the shell itself launches.
    void thePrivilegedConnectionDoesNotLeakToChildren()
    {
        FakeCompositor compositor;
        const QString path = controlSocketPath();
        QVERIFY(compositor.listen(path));

        ShellConnection connection;
        QVERIFY(connection.connectToCompositor(path, 2000));
        QVERIFY(compositor.accept());

        qputenv("SHELL_TEST_CHILD", "privileged");
        ShellProcess process;
        QSignalSpy finished(&process, &QProcess::finished);
        process.startForSession(QCoreApplication::applicationFilePath(), "shell-test-socket",
                                &connection);
        QVERIFY(compositor.receiveFd() >= 0);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        // The child checks its own environment and its grandchild's, and exits
        // non-zero on the first thing that leaked.
        QCOMPARE(process.exitCode(), 0);
    }
};

namespace
{
// Runs as the shell: verifies its own privileged environment, then starts a
// grandchild the way ApplicationRuntime starts an application and verifies that
// nothing privileged came along.
int runPrivilegedChild(QCoreApplication &app)
{
    if (qgetenv("WAYLAND_DISPLAY") != "shell-test-socket")
        return 3;
    if (!qEnvironmentVariableIsSet("WAYLAND_SOCKET"))
        return 4;

    bool valid = false;
    const int fd = qEnvironmentVariable("WAYLAND_SOCKET").toInt(&valid);
    if (!valid || fd < 0)
        return 5;
    // The descriptor really was inherited and really is a socket.
    if (fcntl(fd, F_GETFD) < 0)
        return 6;

    // What the shell does once Qt Wayland has taken the connection over.
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
        return 7;
    unsetenv("WAYLAND_SOCKET");

    QProcess grandchild;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("WAYLAND_SOCKET"));
    environment.insert(QStringLiteral("SHELL_TEST_CHILD"), QStringLiteral("grandchild"));
    environment.insert(QStringLiteral("SHELL_TEST_PRIVILEGED_FD"), QString::number(fd));
    grandchild.setProcessEnvironment(environment);
    grandchild.setProgram(QCoreApplication::applicationFilePath());
    grandchild.start();
    if (!grandchild.waitForFinished(10000))
        return 8;

    Q_UNUSED(app)
    return grandchild.exitCode();
}

// Runs as an application the shell launched.
int runGrandchild()
{
    if (qEnvironmentVariableIsSet("WAYLAND_SOCKET"))
        return 20;
    if (qgetenv("WAYLAND_DISPLAY") != "shell-test-socket")
        return 21;

    // The privileged descriptor must not have been inherited.
    const int fd = qEnvironmentVariable("SHELL_TEST_PRIVILEGED_FD").toInt();
    if (fd > 0 && fcntl(fd, F_GETFD) >= 0)
        return 22;

    return 0;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto mode = qgetenv("SHELL_TEST_CHILD");

    if (mode == "grandchild")
        return runGrandchild();

    if (mode == "privileged")
        return runPrivilegedChild(app);

    if (!mode.isEmpty()) {
        if (qgetenv("WAYLAND_DISPLAY") != "shell-test-socket" ||
            qgetenv("QT_QPA_PLATFORM") != "wayland" ||
            qEnvironmentVariableIsSet("DISPLAY") || qEnvironmentVariableIsSet("WAYLAND_SOCKET"))
            return 2;
        if (mode != "hang")
            QTimer::singleShot(50, &app, &QCoreApplication::quit);
        return app.exec();
    }

    qputenv("DISPLAY", ":99");
    qputenv("WAYLAND_SOCKET", "999");
    qputenv("QT_QPA_PLATFORM", "xcb");
    ShellLifecycleTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "shell_lifecycle_test.moc"
