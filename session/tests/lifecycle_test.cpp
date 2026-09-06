#include "compositorprocess.h"
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <unistd.h>

class LifecycleTest : public QObject {
    Q_OBJECT
private slots:
    void readyAndStop() {
        qputenv("SESSION_TEST_CHILD", "ready");
        CompositorProcess process;
        QSignalSpy ready(&process, &CompositorProcess::ready);
        QSignalSpy failed(&process, &CompositorProcess::failed);
        QSignalSpy exited(&process, &CompositorProcess::exited);
        process.start(QCoreApplication::applicationFilePath(), "test-socket");
        QTRY_COMPARE(ready.count(), 1);
        QVERIFY(process.isReady());
        process.stop();
        QVERIFY(!process.isReady());
        QCOMPARE(failed.count(), 0);
        QCOMPARE(exited.count(), 0);
    }
    void startupFailure() {
        qputenv("SESSION_TEST_CHILD", "exit");
        CompositorProcess process;
        QSignalSpy failed(&process, &CompositorProcess::failed);
        process.start(QCoreApplication::applicationFilePath(), "test-socket");
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!process.isReady());
    }
    void timeout() {
        qputenv("SESSION_TEST_CHILD", "hang");
        CompositorProcess process;
        QSignalSpy failed(&process, &CompositorProcess::failed);
        process.start(QCoreApplication::applicationFilePath(), "test-socket", 30);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!process.isReady());
    }
    void missingExecutable() {
        CompositorProcess process;
        QSignalSpy failed(&process, &CompositorProcess::failed);
        process.start("/does-not-exist/cutefish-desktop", "test-socket");
        QCOMPARE(failed.count(), 1);
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto mode = qgetenv("SESSION_TEST_CHILD");
    if (!mode.isEmpty()) {
        if (mode == "exit") return 1;
        if (app.arguments().size() != 5 || app.arguments().at(2) != "test-socket") return 2;
        if (mode == "ready") {
            const int fd = app.arguments().at(4).toInt();
            if (write(fd, "R", 1) != 1) return 3;
            close(fd);
        }
        return app.exec();
    }
    LifecycleTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "lifecycle_test.moc"
