#include <unistd.h>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include "compositorprocess.h"

class LifecycleTest : public QObject
{
    Q_OBJECT
private slots:

    void readyAndStop()
    {
        qputenv("SESSION_TEST_CHILD", "ready");
        CompositorProcess process;
        QSignalSpy ready(&process, &CompositorProcess::ready);
        QSignalSpy failed(&process, &CompositorProcess::failed);
        QSignalSpy exited(&process, &CompositorProcess::exited);
        process.start(QCoreApplication::applicationFilePath(), "test-socket", "test-control");
        QTRY_COMPARE(ready.count(), 1);
        QVERIFY(process.isReady());
        process.stop();
        QVERIFY(!process.isReady());
        QCOMPARE(failed.count(), 0);
        QCOMPARE(exited.count(), 0);
    }

    void forwardsXwaylandReadinessPath()
    {
        qputenv("SESSION_TEST_CHILD", "xwayland");
        CompositorProcess process;
        QSignalSpy ready(&process, &CompositorProcess::ready);
        process.start(QCoreApplication::applicationFilePath(), "test-socket", "test-control", 1000,
                      "/test-ready.json");
        QTRY_COMPARE(ready.count(), 1);
        process.stop();
    }

    void startupFailure()
    {
        qputenv("SESSION_TEST_CHILD", "exit");
        CompositorProcess process;
        QSignalSpy failed(&process, &CompositorProcess::failed);
        process.start(QCoreApplication::applicationFilePath(), "test-socket", "test-control");
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!process.isReady());
    }

    void timeout()
    {
        qputenv("SESSION_TEST_CHILD", "hang");
        CompositorProcess process;
        QSignalSpy failed(&process, &CompositorProcess::failed);
        process.start(QCoreApplication::applicationFilePath(), "test-socket", "test-control", 30);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!process.isReady());
    }

    void missingExecutable()
    {
        CompositorProcess process;
        QSignalSpy failed(&process, &CompositorProcess::failed);
        process.start("/does-not-exist/cutefish-compositor", "test-socket", "test-control");
        QCOMPARE(failed.count(), 1);
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto mode = qgetenv("SESSION_TEST_CHILD");
    if (!mode.isEmpty()) {
        if (mode == "exit")
            return 1;
        // --socket <name> --ready-fd <fd> --shell-control-socket <path>
        if (app.arguments().size() != (mode == "xwayland" ? 9 : 7) ||
            app.arguments().at(2) != "test-socket" || app.arguments().at(6) != "test-control")
            return 2;
        if (mode == "xwayland" &&
            (app.arguments().at(7) != "--xwayland-ready-file" ||
             app.arguments().at(8) != "/test-ready.json"))
            return 4;
        if (mode == "ready" || mode == "xwayland") {
            const int fd = app.arguments().at(4).toInt();
            if (write(fd, "R", 1) != 1)
                return 3;
            close(fd);
        }
        return app.exec();
    }
    LifecycleTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "lifecycle_test.moc"
