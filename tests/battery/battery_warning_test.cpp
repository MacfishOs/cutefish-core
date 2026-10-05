// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "battery/batterywarning.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QTest>

static const QString displayDevice = QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice");
static const QString deviceInterface = QStringLiteral("org.freedesktop.UPower.Device");

// The fakes live on a connection of their own, so every call crosses the bus.
static QDBusConnection fakeBus()
{
    return QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("fakes"));
}

namespace {

struct Notice {
    uint replacesId;
    QString summary;
    QString body;
    int urgency;
};

} // namespace

class FakeUPower : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UPower")

public:
    QString criticalAction = QStringLiteral("PowerOff");

public slots:
    QString GetCriticalAction() { return criticalAction; }
};

class FakeDisplayDevice : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UPower.Device")
    Q_PROPERTY(uint WarningLevel MEMBER warningLevel)
    Q_PROPERTY(double Percentage MEMBER percentage)
    Q_PROPERTY(qlonglong TimeToEmpty MEMBER timeToEmpty)

public:
    uint warningLevel = BatteryWarning::None;
    double percentage = 80;
    qlonglong timeToEmpty = 0;

    void change(uint level, double percent, qlonglong time = 0)
    {
        warningLevel = level;
        percentage = percent;
        timeToEmpty = time;

        QDBusMessage signal = QDBusMessage::createSignal(displayDevice,
                QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
        signal << deviceInterface
               << QVariantMap {{QStringLiteral("WarningLevel"), warningLevel},
                               {QStringLiteral("Percentage"), percentage},
                               {QStringLiteral("TimeToEmpty"), timeToEmpty}}
               << QStringList();
        fakeBus().send(signal);
    }
};

class FakeNotifications : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    QList<Notice> notices;
    QList<uint> closed;
    int failures = 0;

    void dismiss(uint id)
    {
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/org/freedesktop/Notifications"),
                QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("NotificationClosed"));
        signal << id << 2u;
        fakeBus().send(signal);
    }

public slots:
    uint Notify(const QString &, uint replacesId, const QString &, const QString &summary,
                const QString &body, const QStringList &, const QVariantMap &hints, int)
    {
        if (failures > 0) {
            --failures;
            sendErrorReply(QDBusError::ServiceUnknown, QStringLiteral("No server yet"));
            return 0;
        }

        notices.append({replacesId, summary, body, hints.value(QStringLiteral("urgency")).toInt()});
        return replacesId ? replacesId : uint(notices.size());
    }

    void CloseNotification(uint id) { closed.append(id); }
};

class BatteryWarningTest : public QObject
{
    Q_OBJECT

private:
    FakeUPower m_upower;
    FakeDisplayDevice m_device;
    FakeNotifications m_notifications;

    // Lets a change that should be ignored reach the warning first.
    static void settle() { QTest::qWait(150); }

private slots:
    void initTestCase()
    {
        // Stands in for the system bus, where UPower lives.
        qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));

        QDBusConnection bus = fakeBus();
        QVERIFY(bus.isConnected());
        QVERIFY(bus.registerObject(QStringLiteral("/org/freedesktop/UPower"), &m_upower,
                                   QDBusConnection::ExportAllSlots));
        QVERIFY(bus.registerObject(displayDevice, &m_device, QDBusConnection::ExportAllProperties));
        QVERIFY(bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"), &m_notifications,
                                   QDBusConnection::ExportAllSlots));
        QVERIFY(bus.registerService(QStringLiteral("org.freedesktop.UPower")));
        QVERIFY(bus.registerService(QStringLiteral("org.freedesktop.Notifications")));
    }

    void init()
    {
        m_upower.criticalAction = QStringLiteral("PowerOff");
        m_device.warningLevel = BatteryWarning::None;
        m_device.percentage = 80;
        m_device.timeToEmpty = 0;
        m_notifications.notices.clear();
        m_notifications.closed.clear();
        m_notifications.failures = 0;
    }

    void escalatesInOneNotification()
    {
        BatteryWarning warning;
        settle();
        QCOMPARE(m_notifications.notices.size(), 0);

        m_device.change(BatteryWarning::Low, 20, 65 * 60);
        QTRY_COMPARE(m_notifications.notices.size(), 1);
        Notice notice = m_notifications.notices.at(0);
        QCOMPARE(notice.replacesId, 0u);
        QCOMPARE(notice.summary, QStringLiteral("Low Battery"));
        QCOMPARE(notice.body, QStringLiteral("20% of battery remaining (1h 5m)."));
        QCOMPARE(notice.urgency, 1);

        m_device.change(BatteryWarning::Low, 19, 62 * 60);
        settle();
        QCOMPARE(m_notifications.notices.size(), 1);

        m_device.change(BatteryWarning::Critical, 5);
        QTRY_COMPARE(m_notifications.notices.size(), 2);
        notice = m_notifications.notices.at(1);
        QCOMPARE(notice.replacesId, 1u);
        QCOMPARE(notice.summary, QStringLiteral("Battery Critically Low"));
        QVERIFY(notice.body.contains(QStringLiteral("will shut down")));
        QCOMPARE(notice.urgency, 2);

        m_device.change(BatteryWarning::Action, 2);
        QTRY_COMPARE(m_notifications.notices.size(), 3);
        notice = m_notifications.notices.at(2);
        QCOMPARE(notice.replacesId, 1u);
        QCOMPARE(notice.summary, QStringLiteral("Battery Empty"));
        QVERIFY(notice.body.contains(QStringLiteral("about to shut down")));
        QCOMPARE(notice.urgency, 2);
    }

    void unknownTimeLeavesItOut()
    {
        BatteryWarning warning;
        m_device.change(BatteryWarning::Low, 18);
        QTRY_COMPARE(m_notifications.notices.size(), 1);
        QCOMPARE(m_notifications.notices.at(0).body, QStringLiteral("18% of battery remaining."));
    }

    void powerClosesTheWarning()
    {
        BatteryWarning warning;
        m_device.change(BatteryWarning::Critical, 4);
        QTRY_COMPARE(m_notifications.notices.size(), 1);

        m_device.change(BatteryWarning::None, 4);
        QTRY_COMPARE(m_notifications.closed, QList<uint> {1});

        // The next discharge warns again from the start.
        m_device.change(BatteryWarning::Low, 15);
        QTRY_COMPARE(m_notifications.notices.size(), 2);
        QCOMPARE(m_notifications.notices.at(1).replacesId, 0u);
        QCOMPARE(m_notifications.notices.at(1).summary, QStringLiteral("Low Battery"));
    }

    void dismissedWarningStaysDismissed()
    {
        BatteryWarning warning;
        m_device.change(BatteryWarning::Low, 20);
        QTRY_COMPARE(m_notifications.notices.size(), 1);

        m_notifications.dismiss(1);
        m_device.change(BatteryWarning::Low, 12);
        settle();
        QCOMPARE(m_notifications.notices.size(), 1);

        m_device.change(BatteryWarning::Critical, 5);
        QTRY_COMPARE(m_notifications.notices.size(), 2);
        QCOMPARE(m_notifications.notices.at(1).replacesId, 0u);

        m_device.change(BatteryWarning::None, 5);
        QTRY_COMPARE(m_notifications.closed, QList<uint> {2});
    }

    void startsAtTheCurrentLevel()
    {
        m_device.warningLevel = BatteryWarning::Critical;
        m_device.percentage = 4;
        BatteryWarning warning;
        QTRY_COMPARE(m_notifications.notices.size(), 1);
        QCOMPARE(m_notifications.notices.at(0).summary, QStringLiteral("Battery Critically Low"));
        settle();
        QCOMPARE(m_notifications.notices.size(), 1);
    }

    void namesTheCriticalAction_data()
    {
        QTest::addColumn<QString>("action");
        QTest::addColumn<QString>("critical");
        QTest::addColumn<QString>("empty");

        QTest::newRow("hibernate") << "Hibernate" << "will hibernate" << "about to hibernate";
        QTest::newRow("hybrid sleep") << "HybridSleep" << "will hibernate" << "about to hibernate";
        QTest::newRow("suspend") << "Suspend" << "will go to sleep" << "about to go to sleep";
        QTest::newRow("ignore") << "Ignore" << "Connect to power soon." << "Connect to power now.";
    }

    void namesTheCriticalAction()
    {
        QFETCH(QString, action);
        QFETCH(QString, critical);
        QFETCH(QString, empty);

        m_upower.criticalAction = action;
        BatteryWarning warning;
        m_device.change(BatteryWarning::Critical, 5);
        QTRY_COMPARE(m_notifications.notices.size(), 1);
        QVERIFY2(m_notifications.notices.at(0).body.contains(critical),
                 qPrintable(m_notifications.notices.at(0).body));

        m_device.change(BatteryWarning::Action, 2);
        QTRY_COMPARE(m_notifications.notices.size(), 2);
        QVERIFY2(m_notifications.notices.at(1).body.contains(empty),
                 qPrintable(m_notifications.notices.at(1).body));
    }

    void retriesOnceTheServerAnswers()
    {
        m_notifications.failures = 1;
        BatteryWarning warning;
        m_device.change(BatteryWarning::Low, 20);
        settle();
        QCOMPARE(m_notifications.notices.size(), 0);

        m_device.change(BatteryWarning::Low, 19);
        QTRY_COMPARE(m_notifications.notices.size(), 1);
        QCOMPARE(m_notifications.notices.at(0).summary, QStringLiteral("Low Battery"));
    }
};

QTEST_GUILESS_MAIN(BatteryWarningTest)

#include "battery_warning_test.moc"
