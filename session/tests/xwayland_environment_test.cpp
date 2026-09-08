#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTest>

#include "xwaylandenvironment.h"

class XwaylandEnvironmentTest : public QObject
{
    Q_OBJECT
private:
    static void publish(const QString& path, const QString& display, const QString& authority)
    {
        QSaveFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(
            QJsonDocument(QJsonObject {{"DISPLAY", display}, {"XAUTHORITY", authority}}).toJson());
        QVERIFY(file.commit());
    }

private slots:

    void arrivalOrdersAndLoss()
    {
        for (bool early : {false, true}) {
            QTemporaryDir directory;
            const QString authority = directory.filePath("auth");
            QFile file(authority);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner));
            file.close();
            XwaylandEnvironment environment;
            QSignalSpy ready(&environment, &XwaylandEnvironment::resolved);
            QSignalSpy lost(&environment, &XwaylandEnvironment::lost);
            qputenv("DISPLAY", ":old");
            qputenv("XAUTHORITY", "/old");
            if (early)
                publish(environment.readinessFile(), ":123", authority);
            environment.waitForReady(1000);
            if (!early) {
                QVERIFY(qEnvironmentVariableIsEmpty("DISPLAY"));
                publish(environment.readinessFile(), ":123", authority);
            }
            QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 1500);
            QVERIFY(ready.first().first().toBool());
            QCOMPARE(qgetenv("DISPLAY"), QByteArray(":123"));
            QCOMPARE(qgetenv("XAUTHORITY"), authority.toUtf8());
            QVERIFY(QFile::remove(environment.readinessFile()));
            QTRY_COMPARE_WITH_TIMEOUT(lost.size(), 1, 1500);
            QVERIFY(qEnvironmentVariableIsEmpty("DISPLAY"));
            QVERIFY(qEnvironmentVariableIsEmpty("XAUTHORITY"));
        }
    }

    void malformedAndTimeout()
    {
        XwaylandEnvironment environment;
        QSignalSpy ready(&environment, &XwaylandEnvironment::resolved);
        publish(environment.readinessFile(), "remote:0", "/missing");
        environment.waitForReady(30);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 1000);
        QVERIFY(!ready.first().first().toBool());
        QVERIFY(qEnvironmentVariableIsEmpty("DISPLAY"));
    }

    void realCompositor()
    {
        const auto binary = qEnvironmentVariable("CUTEFISH_TEST_COMPOSITOR");
        if (binary.isEmpty())
            QSKIP("Set CUTEFISH_TEST_COMPOSITOR for real Xwayland integration");
        QTemporaryDir runtime;
        XwaylandEnvironment environment;
        QSignalSpy ready(&environment, &XwaylandEnvironment::resolved);
        QProcess compositor;
        auto childEnvironment = QProcessEnvironment::systemEnvironment();
        childEnvironment.insert("XDG_RUNTIME_DIR", runtime.path());
        compositor.setProcessEnvironment(childEnvironment);
        compositor.start(binary,
                         {"--no-drm", "--socket", "session-test", "--xwayland-shm",
                          "--xwayland-ready-file", environment.readinessFile()});
        environment.waitForReady(10000);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 12000);
        QVERIFY2(ready.first().first().toBool(), compositor.readAllStandardError().constData());
        QProcess client;
        client.start("xdpyinfo", {});
        QVERIFY(client.waitForFinished(3000));
        QCOMPARE(client.exitCode(), 0);
        compositor.terminate();
        QVERIFY(compositor.waitForFinished(5000));
        QCOMPARE(compositor.exitCode(), 0);
    }

    void disabledXwaylandKeepsNativeCompositor()
    {
        const auto binary = qEnvironmentVariable("CUTEFISH_TEST_COMPOSITOR");
        if (binary.isEmpty())
            QSKIP("Set CUTEFISH_TEST_COMPOSITOR for real integration");
        QTemporaryDir runtime;
        XwaylandEnvironment environment;
        QSignalSpy ready(&environment, &XwaylandEnvironment::resolved);
        QProcess compositor;
        auto childEnvironment = QProcessEnvironment::systemEnvironment();
        childEnvironment.insert("XDG_RUNTIME_DIR", runtime.path());
        compositor.setProcessEnvironment(childEnvironment);
        compositor.start(binary,
                         {"--no-drm", "--socket", "session-test", "--no-xwayland",
                          "--xwayland-ready-file", environment.readinessFile()});
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(runtime.filePath("session-test")), 3000);
        environment.waitForReady(30);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 1000);
        QVERIFY(!ready.first().first().toBool());
        QCOMPARE(compositor.state(), QProcess::Running);
        compositor.terminate();
        QVERIFY(compositor.waitForFinished(5000));
        QCOMPARE(compositor.exitCode(), 0);
    }

    void unsafeAuthority()
    {
        QTemporaryDir directory;
        QFile file(directory.filePath("auth"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadOther));
        file.close();
        XwaylandEnvironment environment;
        QSignalSpy ready(&environment, &XwaylandEnvironment::resolved);
        publish(environment.readinessFile(), ":123", file.fileName());
        environment.waitForReady(30);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 1000);
        QVERIFY(!ready.first().first().toBool());
    }
};
QTEST_GUILESS_MAIN(XwaylandEnvironmentTest)
#include "xwayland_environment_test.moc"
