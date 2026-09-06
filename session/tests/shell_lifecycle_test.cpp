#include "shellprocess.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

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
        process.startForSession(QCoreApplication::applicationFilePath(), "shell-test-socket");
        process.startForSession("/does-not-exist/duplicate-shell", "wrong-socket");
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(started.count(), 1);
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
    }

    void stopAndMissingExecutable()
    {
        qputenv("SHELL_TEST_CHILD", "hang");
        ShellProcess process;
        process.startForSession(QCoreApplication::applicationFilePath(), "shell-test-socket");
        QVERIFY(process.waitForStarted());
        process.stop();
        QCOMPARE(process.state(), QProcess::NotRunning);
        process.stop();

        QSignalSpy failed(&process, &QProcess::errorOccurred);
        process.startForSession("/does-not-exist/cutefish-shell", "shell-test-socket");
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(process.error(), QProcess::FailedToStart);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto mode = qgetenv("SHELL_TEST_CHILD");
    if (!mode.isEmpty()) {
        if (qgetenv("WAYLAND_DISPLAY") != "shell-test-socket" ||
            qgetenv("QT_QPA_PLATFORM") != "wayland" ||
            qgetenv("QT_QUICK_BACKEND") != "software" ||
            qEnvironmentVariableIsSet("DISPLAY") || qEnvironmentVariableIsSet("WAYLAND_SOCKET"))
            return 2;
        if (mode != "hang")
            QTimer::singleShot(50, &app, &QCoreApplication::quit);
        return app.exec();
    }
    qputenv("DISPLAY", ":99");
    qputenv("WAYLAND_SOCKET", "999");
    qputenv("QT_QPA_PLATFORM", "xcb");
    qputenv("QT_QUICK_BACKEND", "opengl");
    ShellLifecycleTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "shell_lifecycle_test.moc"
