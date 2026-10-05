#include "power/dimdisplayaction.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QMutex>
#include <QTest>
#include <QThread>
#include <QTimer>

using namespace cutefish::display;

static const QString displayService = QStringLiteral("com.cutefish.DisplayConfig.PowerTest");

namespace {

// What the fakes were asked, read from the test thread.
struct Log {
    int locks = 0;
    int suspends = 0;
    QList<bool> docked;
    QList<uint> power;
};

class Recorder
{
public:
    Log log() const
    {
        QMutexLocker locker(&m_mutex);
        return m_log;
    }

    template<typename F>
    void update(F &&change)
    {
        QMutexLocker locker(&m_mutex);
        change(m_log);
    }

private:
    mutable QMutex m_mutex;
    Log m_log;
};

class FakeScreenSaver : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.ScreenSaver")

public:
    explicit FakeScreenSaver(Recorder *recorder)
        : m_recorder(recorder)
    {
    }

    bool fails = false;

public slots:
    void Lock()
    {
        m_recorder->update([](Log &log) {
            ++log.locks;
        });
        if (fails)
            sendErrorReply(QDBusError::Failed, QStringLiteral("Session is not available"));
    }

signals:
    void ActiveChanged(bool active);

private:
    Recorder *m_recorder;
};

class FakeSession : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.cutefish.Session")

public:
    explicit FakeSession(Recorder *recorder)
        : m_recorder(recorder)
    {
    }

public slots:
    bool suspend()
    {
        m_recorder->update([](Log &log) {
            ++log.suspends;
        });
        return true;
    }

private:
    Recorder *m_recorder;
};

// Answers the way the compositor's DisplayConfigService does.
class FakeDisplayConfig : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.cutefish.DisplayConfig1")

public:
    explicit FakeDisplayConfig(Recorder *recorder)
        : m_recorder(recorder)
    {
        ipc::registerTypes();
    }

    bool external = false;
    int busyAnswers = 0;

    void changeTopology()
    {
        ++m_generation;
        ++m_revision;
        emit StateChanged(m_generation, m_revision);
    }

public slots:
    // Qt D-Bus matches output types by their full names.
    QString GetState(QString &seatId, qulonglong &topologyGeneration, qulonglong &appliedRevision,
                     QList<cutefish::display::ipc::PhysicalDisplay> &physical,
                     QList<cutefish::display::ipc::LogicalDisplay> &logical,
                     QList<cutefish::display::ipc::Placement> &placements, QString &previewToken,
                     qulonglong &previewDeadlineMs)
    {
        seatId = QStringLiteral("seat0");
        topologyGeneration = m_generation;
        appliedRevision = m_revision;
        physical.clear();
        logical.clear();
        placements.clear();
        previewToken.clear();
        previewDeadlineMs = 0;
        return QStringLiteral("test-instance");
    }

    QString SetDockedLidClosed(bool closed)
    {
        m_recorder->update([closed](Log &log) {
            log.docked.append(closed);
        });
        if (closed && busyAnswers > 0) {
            --busyAnswers;
            return reject(ipc::ErrorCode::Busy);
        }
        if (closed && !external)
            return reject(ipc::ErrorCode::NoEnabledOutput);
        return succeed();
    }

    QString SetDisplayPower(const QStringList &, uint state)
    {
        m_recorder->update([state](Log &log) {
            log.power.append(state);
        });
        return succeed();
    }

signals:
    void StateChanged(qulonglong topologyGeneration, qulonglong appliedRevision);
    void OperationFinished(const QString &token, bool succeeded, bool degraded, uint errorCode,
                           const QString &message, const QStringList &affectedDisplayIds,
                           qulonglong topologyGeneration, qulonglong appliedRevision);

private:
    QString reject(ipc::ErrorCode error)
    {
        const QString token = nextToken();
        emit OperationFinished(token, false, false, uint(error), {}, {}, m_generation, m_revision);
        return token;
    }

    QString succeed()
    {
        const QString token = nextToken();
        QTimer::singleShot(0, this, [this, token] {
            emit OperationFinished(token, true, false, 0, {}, {}, m_generation, m_revision);
        });
        return token;
    }

    QString nextToken() { return QStringLiteral("token-%1").arg(++m_serial); }

    Recorder *m_recorder;
    qulonglong m_generation = 1;
    qulonglong m_revision = 1;
    int m_serial = 0;
};

// The action blocks on Lock and suspend, so the fakes answer from their own thread.
class Services : public QObject
{
    Q_OBJECT

public:
    Services()
    {
        m_thread.start();
        moveToThread(&m_thread);
        run([this] {
            m_bus = std::make_unique<QDBusConnection>(QDBusConnection::connectToBus(
                    QDBusConnection::SessionBus, QStringLiteral("fake-services")));
            screenSaver = new FakeScreenSaver(&recorder);
            session = new FakeSession(&recorder);
            display = new FakeDisplayConfig(&recorder);
            const auto options =
                    QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals;
            m_bus->registerObject(QStringLiteral("/ScreenSaver"), screenSaver, options);
            m_bus->registerService(QStringLiteral("org.freedesktop.ScreenSaver"));
            m_bus->registerObject(QStringLiteral("/Session"), session, options);
            m_bus->registerService(QStringLiteral("com.cutefish.Session"));
            m_bus->registerObject(QStringLiteral("/com/cutefish/DisplayConfig1"), display,
                                  options);
            m_bus->registerService(displayService);
        });
    }

    ~Services() override
    {
        run([this] {
            m_bus->unregisterService(QStringLiteral("org.freedesktop.ScreenSaver"));
            m_bus->unregisterService(QStringLiteral("com.cutefish.Session"));
            m_bus->unregisterService(displayService);
            delete screenSaver;
            delete session;
            delete display;
            const QString name = m_bus->name();
            m_bus.reset();
            QDBusConnection::disconnectFromBus(name);
            moveToThread(QCoreApplication::instance()->thread());
        });
        m_thread.quit();
        m_thread.wait();
    }

    template<typename F>
    void run(F &&function)
    {
        QMetaObject::invokeMethod(this, std::forward<F>(function), Qt::BlockingQueuedConnection);
    }

    Recorder recorder;
    FakeScreenSaver *screenSaver = nullptr;
    FakeSession *session = nullptr;
    FakeDisplayConfig *display = nullptr;

private:
    QThread m_thread;
    std::unique_ptr<QDBusConnection> m_bus;
};

} // namespace

class PowerPolicyTest : public QObject
{
    Q_OBJECT

    // Lets the action's display client load the compositor's state first.
    static void waitForDisplayState() { QTest::qWait(100); }

    static void setLocked(Services &services, bool locked)
    {
        services.run([&] {
            emit services.screenSaver->ActiveChanged(locked);
        });
        QTest::qWait(100);
    }

private slots:
    void initTestCase()
    {
        // No logind here: the lid inhibitor fails instead of reaching the real one.
        qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
        // Nor a compositor: idle notifications never arrive, the tests deliver them.
        qputenv("WAYLAND_DISPLAY", "power-policy-test-none");
        qputenv("CUTEFISH_DISPLAY_CONFIG_SERVICE", displayService.toUtf8());
    }

    void singleDisplayLidLocksAndSuspends()
    {
        Services services;
        DimDisplayAction action;
        action.setLidPresent(true);
        waitForDisplayState();

        action.handleLidClosed();
        QTRY_COMPARE(services.recorder.log().suspends, 1);
        const Log log = services.recorder.log();
        QCOMPARE(log.docked, QList<bool> {true});
        QCOMPARE(log.locks, 1);
        QTRY_COMPARE(services.recorder.log().power, QList<uint> {1});

        action.handleLidOpened();
        QTRY_COMPARE(services.recorder.log().power, (QList<uint> {1, 0}));
    }

    void failedLockStillSuspends()
    {
        Services services;
        services.run([&] {
            services.screenSaver->fails = true;
        });
        DimDisplayAction action;
        action.setLidPresent(true);
        waitForDisplayState();

        action.handleLidClosed();
        QTRY_COMPARE(services.recorder.log().suspends, 1);
        QCOMPARE(services.recorder.log().locks, 1);
        QTRY_COMPARE(services.recorder.log().power, QList<uint> {1});
    }

    void dockedLidKeepsTheSessionAwake()
    {
        Services services;
        services.run([&] {
            services.display->external = true;
        });
        DimDisplayAction action;
        action.setLidPresent(true);
        waitForDisplayState();

        action.handleLidClosed();
        QTRY_COMPARE(services.recorder.log().docked, QList<bool> {true});
        QTest::qWait(200);
        Log log = services.recorder.log();
        QCOMPARE(log.locks, 0);
        QCOMPARE(log.suspends, 0);
        QVERIFY(log.power.isEmpty());

        action.handleLidOpened();
        QTRY_COMPARE(services.recorder.log().docked, (QList<bool> {true, false}));
        log = services.recorder.log();
        QCOMPARE(log.suspends, 0);
        QVERIFY(log.power.isEmpty());
    }

    void losingTheExternalDisplaySuspends()
    {
        Services services;
        services.run([&] {
            services.display->external = true;
        });
        DimDisplayAction action;
        action.setLidPresent(true);
        waitForDisplayState();
        action.handleLidClosed();
        QTRY_COMPARE(services.recorder.log().docked, QList<bool> {true});
        QTest::qWait(100);

        services.run([&] {
            services.display->external = false;
            services.display->changeTopology();
        });
        QTRY_COMPARE(services.recorder.log().suspends, 1);
        const Log log = services.recorder.log();
        QCOMPARE(log.docked, (QList<bool> {true, true}));
        // The panel is already off: the lock comes from powering everything off.
        QCOMPARE(log.locks, 0);
        QTRY_COMPARE(services.recorder.log().power, QList<uint> {1});

        action.handleLidOpened();
        QTRY_COMPARE(services.recorder.log().power, (QList<uint> {1, 0}));
    }

    void keepingAnotherExternalDisplayStaysDocked()
    {
        Services services;
        services.run([&] {
            services.display->external = true;
        });
        DimDisplayAction action;
        action.setLidPresent(true);
        waitForDisplayState();
        action.handleLidClosed();
        QTRY_COMPARE(services.recorder.log().docked, QList<bool> {true});
        QTest::qWait(100);

        services.run([&] {
            services.display->changeTopology();
        });
        QTRY_COMPARE(services.recorder.log().docked, (QList<bool> {true, true}));
        QTest::qWait(200);
        QCOMPARE(services.recorder.log().suspends, 0);
    }

    void aDisplayChangeInProgressIsWaitedOut()
    {
        Services services;
        services.run([&] {
            services.display->external = true;
            services.display->busyAnswers = 2;
        });
        DimDisplayAction action;
        action.setLidPresent(true);
        waitForDisplayState();

        action.handleLidClosed();
        QTRY_COMPARE_WITH_TIMEOUT(services.recorder.log().docked, (QList<bool> {true, true, true}),
                                  3000);
        QTest::qWait(200);
        const Log log = services.recorder.log();
        QCOMPARE(log.suspends, 0);
        QCOMPARE(log.locks, 0);
    }

    void aDisplayChangeThatNeverSettlesStillSuspends()
    {
        Services services;
        services.run([&] {
            services.display->external = true;
            services.display->busyAnswers = 1000;
        });
        DimDisplayAction action;
        action.setLidPresent(true);
        waitForDisplayState();

        action.handleLidClosed();
        QTRY_COMPARE_WITH_TIMEOUT(services.recorder.log().suspends, 1, 10000);
        QCOMPARE(services.recorder.log().docked.size(), 11);
    }

    void aLockedScreenGoesDarkSoonAndSleepsEvenWithNever()
    {
        Services services;
        DimDisplayAction action;
        waitForDisplayState();
        action.setPolicy(-1, 1);
        setLocked(services, true);

        action.onIdleTimeout(30000);
        QTRY_COMPARE(services.recorder.log().power, QList<uint> {1});
        QTRY_COMPARE(services.recorder.log().suspends, 1);
    }

    void anUnlockedScreenKeepsTheUsersTimeout()
    {
        Services services;
        DimDisplayAction action;
        waitForDisplayState();
        action.setPolicy(1200, 1);

        action.onIdleTimeout(30000);
        QTest::qWait(200);
        QVERIFY(services.recorder.log().power.isEmpty());
    }

    void lockingAfterTheUserLeftGoesDarkAtOnce()
    {
        Services services;
        DimDisplayAction action;
        waitForDisplayState();
        action.setPolicy(1200, 900);
        action.onIdleTimeout(30000);
        QTest::qWait(100);
        QVERIFY(services.recorder.log().power.isEmpty());

        setLocked(services, true);
        QTRY_COMPARE(services.recorder.log().power, QList<uint> {1});
    }

    void wakingTheLockScreenLightsItAgain()
    {
        Services services;
        DimDisplayAction action;
        waitForDisplayState();
        action.setPolicy(1200, 900);
        setLocked(services, true);
        action.onIdleTimeout(30000);
        QTRY_COMPARE(services.recorder.log().power, QList<uint> {1});

        action.onWakeupFromIdle();
        QTRY_COMPARE(services.recorder.log().power, (QList<uint> {1, 0}));
        action.onIdleTimeout(30000);
        QTRY_COMPARE(services.recorder.log().power, (QList<uint> {1, 0, 1}));
    }

    void wakingWhileDockedLetsTheDisplayGoDarkAgain()
    {
        Services services;
        services.run([&] {
            services.display->external = true;
        });
        DimDisplayAction action;
        action.setLidPresent(true);
        waitForDisplayState();
        action.handleLidClosed();
        QTRY_COMPARE(services.recorder.log().docked, QList<bool> {true});
        QTest::qWait(100);

        action.setPolicy(1200, 900);
        action.onIdleTimeout(1200000);
        QTRY_COMPARE(services.recorder.log().power, QList<uint> {1});

        action.onWakeupFromIdle();
        QTest::qWait(100);
        action.onIdleTimeout(1200000);
        QTRY_COMPARE(services.recorder.log().power, (QList<uint> {1, 1}));
        // Powering every display on would light the closed panel.
        QVERIFY(!services.recorder.log().power.contains(0u));
    }

    void preventingSleepKeepsTheSystemAwake()
    {
        Services services;
        DimDisplayAction action;
        waitForDisplayState();
        action.setPolicy(-1, 0);
        setLocked(services, true);

        action.onIdleTimeout(30000);
        QTRY_COMPARE(services.recorder.log().power, QList<uint> {1});
        QTest::qWait(1500);
        QCOMPARE(services.recorder.log().suspends, 0);
    }
};

QTEST_GUILESS_MAIN(PowerPolicyTest)

#include "power_policy_test.moc"
