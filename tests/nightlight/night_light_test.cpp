// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nightlight.h"
#include "nightlightschedule.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTest>

#include <cstdlib>
#include <memory>

#include "displaytypes.h"
#include "nightlightadaptor.h"
#include "preferences.h"

using namespace cutefish::display;
using namespace NightLightSchedule;

namespace {

const QString displayService = QStringLiteral("com.cutefish.DisplayConfig.NightLightTest");

QDateTime at(int day, int hour, int minute = 0)
{
    return QDateTime(QDate(2026, 10, day), QTime(hour, minute), QTimeZone::UTC);
}

TimesForDate fixed(QTime start, QTime end)
{
    return [start, end](QDate) {
        return Times {start, end};
    };
}

int minutesBetween(const QDateTime &a, const QTime &b)
{
    return std::abs(a.time().secsTo(b)) / 60;
}

struct SetNightLightCall {
    double strength;
    uint durationMs;
};

// Answers the way the compositor's DisplayConfigService does.
class FakeDisplayConfig : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.cutefish.DisplayConfig1")

public:
    FakeDisplayConfig() { ipc::registerTypes(); }

    bool tintable = true;
    QList<SetNightLightCall> calls;

public slots:
    // Qt D-Bus matches output types by their full names.
    QString GetState(QString &seatId, qulonglong &topologyGeneration, qulonglong &appliedRevision,
                     QList<cutefish::display::ipc::PhysicalDisplay> &physical,
                     QList<cutefish::display::ipc::LogicalDisplay> &logical,
                     QList<cutefish::display::ipc::Placement> &placements, QString &previewToken,
                     qulonglong &previewDeadlineMs)
    {
        seatId = QStringLiteral("seat0");
        topologyGeneration = 1;
        appliedRevision = 1;
        ipc::PhysicalDisplay display;
        display.sessionId = QStringLiteral("internal");
        display.capabilities = tintable ? ipc::ColorTransformCapability : 0;
        physical = {display};
        logical.clear();
        placements.clear();
        previewToken.clear();
        previewDeadlineMs = 0;
        return QStringLiteral("test-instance");
    }

    void SetNightLight(double strength, uint durationMs)
    {
        calls.append({strength, durationMs});
    }

signals:
    void StateChanged(qulonglong topologyGeneration, qulonglong appliedRevision);
};

} // namespace

class NightLightTest : public QObject
{
    Q_OBJECT

private:
    QDBusConnection m_bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                         QStringLiteral("night-light-compositor"));
    std::unique_ptr<FakeDisplayConfig> m_display;
    QDateTime m_now = at(2, 12);

    void startCompositor(bool tintable = true)
    {
        m_display = std::make_unique<FakeDisplayConfig>();
        m_display->tintable = tintable;
        m_bus.registerObject(QStringLiteral("/com/cutefish/DisplayConfig1"), m_display.get(),
                             QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
        QVERIFY(m_bus.registerService(displayService));
    }

    void stopCompositor()
    {
        m_bus.unregisterService(displayService);
        m_bus.unregisterObject(QStringLiteral("/com/cutefish/DisplayConfig1"));
        m_display.reset();
    }

    std::unique_ptr<NightLight> start()
    {
        return std::make_unique<NightLight>(nullptr, [this] {
            return m_now;
        });
    }

    QVariant saved(const QString &key)
    {
        QSettings settings(Preferences::file(QStringLiteral("services")), QSettings::IniFormat);
        return settings.value(QStringLiteral("NightLight/") + key);
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        // DisplayClient's service name for a compositor under test.
        qputenv("CUTEFISH_DISPLAY_CONFIG_SERVICE", displayService.toUtf8());
        QVERIFY(m_bus.isConnected());
    }

    void init()
    {
        QFile::remove(Preferences::file(QStringLiteral("services")));
        m_now = at(2, 12);
        startCompositor();
    }

    void cleanup() { stopCompositor(); }

    // --- Where a day stands on a schedule. ---

    void anOvernightScheduleIsOnAcrossMidnight()
    {
        const auto times = fixed(QTime(18, 0), QTime(6, 0));
        auto position = NightLightSchedule::position(at(2, 12), times);
        QVERIFY(!position.enabled);
        QCOMPARE(position.next, at(2, 18));
        position = NightLightSchedule::position(at(2, 18), times);
        QVERIFY(position.enabled);
        QCOMPARE(position.next, at(3, 6));
        position = NightLightSchedule::position(at(3, 3), times);
        QVERIFY(position.enabled);
        QCOMPARE(position.next, at(3, 6));
        position = NightLightSchedule::position(at(3, 6), times);
        QVERIFY(!position.enabled);
        QCOMPARE(position.next, at(3, 18));
    }

    void aDaytimeScheduleIsOnBetweenItsTimes()
    {
        const auto times = fixed(QTime(9, 0), QTime(17, 0));
        auto position = NightLightSchedule::position(at(2, 8), times);
        QVERIFY(!position.enabled);
        QCOMPARE(position.next, at(2, 9));
        position = NightLightSchedule::position(at(2, 12), times);
        QVERIFY(position.enabled);
        QCOMPARE(position.next, at(2, 17));
        position = NightLightSchedule::position(at(2, 20), times);
        QVERIFY(!position.enabled);
        QCOMPARE(position.next, at(3, 9));
    }

    void equalTimesNeverTurnItOn()
    {
        const auto position = NightLightSchedule::position(at(2, 12), fixed(QTime(7, 0), QTime(7, 0)));
        QVERIFY(!position.enabled);
        QVERIFY(!position.next.isValid());
    }

    // Tomorrow's sunrise is not today's: the turn comes from the day it falls on.
    void theNextTurnUsesItsOwnDaysTimes()
    {
        const TimesForDate times = [](QDate date) {
            return Times {QTime(18, 0), QTime(6, date.day())};
        };
        const auto position = NightLightSchedule::position(at(2, 22), times);
        QVERIFY(position.enabled);
        QCOMPARE(position.next, at(3, 6, 3));
    }

    // Europe/Berlin repeats 02:00 to 03:00 on 2026-10-25: each 02:30 waits for its own 02:45.
    void aRepeatedHourTurnsAtItsOwnOccurrence()
    {
        using Resolution = QDateTime::TransitionResolution;
        const QTimeZone zone("Europe/Berlin");
        const QDate date(2026, 10, 25);
        const auto times = fixed(QTime(18, 0), QTime(2, 45));
        const QDateTime first(date, QTime(2, 30), zone, Resolution::PreferBefore);
        const QDateTime second(date, QTime(2, 30), zone, Resolution::PreferAfter);
        QCOMPARE(first.secsTo(second), 3600);

        auto position = NightLightSchedule::position(first, times);
        QVERIFY(position.enabled);
        QCOMPARE(position.next, QDateTime(date, QTime(2, 45), zone, Resolution::PreferBefore));

        position = NightLightSchedule::position(second, times);
        QVERIFY(position.enabled);
        QVERIFY(position.next > second);
        QCOMPARE(position.next, QDateTime(date, QTime(2, 45), zone, Resolution::PreferAfter));
    }

    // Against NOAA's solar calculator and the Julian-date sunrise equation,
    // which agree to a few minutes.
    void sunTimesMatchTheAlmanac()
    {
        auto sun = sunTimes(QDate(2026, 6, 21), 31.2333, 121.4667, QTimeZone("Asia/Shanghai"));
        QVERIFY(sun);
        QVERIFY(minutesBetween(sun->sunrise, QTime(4, 50)) <= 3);
        QVERIFY(minutesBetween(sun->sunset, QTime(19, 0)) <= 3);
        QCOMPARE(sun->sunrise.date(), QDate(2026, 6, 21));

        sun = sunTimes(QDate(2026, 12, 21), 51.5083, -0.1253, QTimeZone("Europe/London"));
        QVERIFY(sun);
        QVERIFY(minutesBetween(sun->sunrise, QTime(8, 3)) <= 3);
        QVERIFY(minutesBetween(sun->sunset, QTime(15, 53)) <= 3);

        sun = sunTimes(QDate(2026, 7, 4), 40.7142, -74.0064, QTimeZone("America/New_York"));
        QVERIFY(sun);
        QVERIFY(minutesBetween(sun->sunrise, QTime(5, 29)) <= 3);
        QVERIFY(minutesBetween(sun->sunset, QTime(20, 30)) <= 3);
        QCOMPARE(sun->sunset.date(), QDate(2026, 7, 4));
    }

    void thePolarDayAndNightHaveNoTurns()
    {
        QVERIFY(!sunTimes(QDate(2026, 6, 21), 69.6489, 18.9551, QTimeZone("Europe/Oslo")));
        QVERIFY(!sunTimes(QDate(2026, 12, 21), 69.6489, 18.9551, QTimeZone("Europe/Oslo")));
    }

    void zoneTablesGiveTheZonesCity()
    {
        QTemporaryFile table;
        QVERIFY(table.open());
        table.write("# comment\n"
                    "CN\t+3114+12128\tAsia/Shanghai\tBeijing Time\n"
                    "US\t+404251-0740023\tAmerica/New_York\tEastern (most areas)\n"
                    "XX\tnonsense\tBroken/Zone\n");
        table.close();

        auto coordinates = zoneCoordinates("Asia/Shanghai", {table.fileName()});
        QVERIFY(coordinates);
        QVERIFY(qAbs(coordinates->latitude - (31 + 14 / 60.0)) < 1e-9);
        QVERIFY(qAbs(coordinates->longitude - (121 + 28 / 60.0)) < 1e-9);
        coordinates = zoneCoordinates("America/New_York", {table.fileName()});
        QVERIFY(coordinates);
        QVERIFY(qAbs(coordinates->latitude - (40 + 42 / 60.0 + 51 / 3600.0)) < 1e-9);
        QVERIFY(qAbs(coordinates->longitude + (74 + 0 / 60.0 + 23 / 3600.0)) < 1e-9);
        QVERIFY(!zoneCoordinates("Broken/Zone", {table.fileName()}));
        QVERIFY(!zoneCoordinates("UTC", {table.fileName()}));
    }

    // --- The service. ---

    void availabilityFollowsTheCompositorsDisplays()
    {
        auto nightLight = start();
        QTRY_VERIFY(nightLight->available());

        stopCompositor();
        QTRY_VERIFY(!nightLight->available());
        startCompositor(false);
        QTest::qWait(100);
        QVERIFY(!nightLight->available());
    }

    void theSavedStateReachesTheCompositorOnceItIsLoaded()
    {
        {
            QSettings settings(Preferences::file(QStringLiteral("services")), QSettings::IniFormat);
            settings.setValue(QStringLiteral("NightLight/Enabled"), true);
            settings.setValue(QStringLiteral("NightLight/Temperature"), 0.3);
        }
        auto nightLight = start();
        QTRY_COMPARE(m_display->calls.size(), 1);
        QCOMPARE(m_display->calls.first().strength, 0.3);
        QCOMPARE(m_display->calls.first().durationMs, 1000u);

        // A compositor that starts again is told again.
        stopCompositor();
        startCompositor();
        QTRY_COMPARE(m_display->calls.size(), 1);
        QCOMPARE(m_display->calls.first().strength, 0.3);
    }

    void aManualToggleTakesASecondAndIsSaved()
    {
        auto nightLight = start();
        QTRY_VERIFY(nightLight->available());
        QSignalSpy changed(nightLight.get(), &NightLight::enabledChanged);

        nightLight->setEnabled(true);
        QCOMPARE(changed.size(), 1);
        QTRY_COMPARE(m_display->calls.size(), 2);
        QCOMPARE(m_display->calls.last().strength, 0.5);
        QCOMPARE(m_display->calls.last().durationMs, 1000u);
        QCOMPARE(saved(QStringLiteral("Enabled")).toBool(), true);

        nightLight->setEnabled(false);
        QTRY_COMPARE(m_display->calls.size(), 3);
        QCOMPARE(m_display->calls.last().strength, 0.0);
    }

    void aTemperatureChangedWhileOffIsOnlySaved()
    {
        auto nightLight = start();
        QTRY_VERIFY(nightLight->available());
        QTRY_COMPARE(m_display->calls.size(), 1);

        nightLight->setTemperature(0.8);
        QTest::qWait(100);
        QCOMPARE(m_display->calls.size(), 1);
        QCOMPARE(saved(QStringLiteral("Temperature")).toDouble(), 0.8);

        nightLight->setEnabled(true);
        QTRY_COMPARE(m_display->calls.size(), 2);
        QCOMPARE(m_display->calls.last().strength, 0.8);
        nightLight->setTemperature(2);
        QCOMPARE(nightLight->temperature(), 1.0);
    }

    void aPreviewShowsTheTemperatureWhileOff()
    {
        auto nightLight = start();
        QTRY_VERIFY(nightLight->available());
        QTRY_COMPARE(m_display->calls.size(), 1);

        nightLight->setPreview(true);
        QTRY_COMPARE(m_display->calls.size(), 2);
        QCOMPARE(m_display->calls.last().strength, 0.5);
        QCOMPARE(m_display->calls.last().durationMs, 1000u);

        nightLight->setTemperature(0.8);
        QTRY_COMPARE(m_display->calls.size(), 3);
        QCOMPARE(m_display->calls.last().strength, 0.8);
        QCOMPARE(m_display->calls.last().durationMs, 100u);

        nightLight->setPreview(false);
        QTRY_COMPARE(m_display->calls.size(), 4);
        QCOMPARE(m_display->calls.last().strength, 0.0);
        QCOMPARE(m_display->calls.last().durationMs, 1000u);
        QVERIFY(!nightLight->enabled());
        QCOMPARE(saved(QStringLiteral("Temperature")).toDouble(), 0.8);
        QCOMPARE(saved(QStringLiteral("Enabled")).toBool(), false);
    }

    void aPreviewWhileOnOnlyQuickensTheSlider()
    {
        auto nightLight = start();
        QTRY_VERIFY(nightLight->available());
        nightLight->setEnabled(true);
        QTRY_COMPARE(m_display->calls.size(), 2);

        nightLight->setPreview(true);
        nightLight->setTemperature(0.8);
        QTRY_COMPARE(m_display->calls.size(), 3);
        QCOMPARE(m_display->calls.last().durationMs, 100u);
        nightLight->setPreview(false);
        QTest::qWait(100);
        QCOMPARE(m_display->calls.size(), 3);
        QCOMPARE(m_display->calls.last().strength, 0.8);
    }

    void aPreviewEndsWhenItsCallerLeaves()
    {
        auto nightLight = start();
        QTRY_VERIFY(nightLight->available());
        QTRY_COMPARE(m_display->calls.size(), 1);
        const QString path = QStringLiteral("/com/cutefish/Services/NightLight");
        // The service's own route: through the adaptor, which must reach QDBusContext.
        new NightLightAdaptor(nightLight.get());
        QDBusConnection service = QDBusConnection::sessionBus();
        QVERIFY(service.registerObject(path, nightLight.get()));

        const QString name = QStringLiteral("night-light-settings");
        {
            // The connection closes only once no handle refers to it.
            QDBusConnection settings =
                    QDBusConnection::connectToBus(QDBusConnection::SessionBus, name);
            QDBusMessage message = QDBusMessage::createMethodCall(
                    service.baseService(), path, QStringLiteral("com.cutefish.Services.NightLight"),
                    QStringLiteral("setPreview"));
            message << true;
            settings.asyncCall(message);
        }
        QTRY_VERIFY(nightLight->previewing());
        QTRY_COMPARE(m_display->calls.size(), 2);
        QCOMPARE(m_display->calls.last().strength, 0.5);

        QDBusConnection::disconnectFromBus(name);
        QTRY_VERIFY(!nightLight->previewing());
        QTRY_COMPARE(m_display->calls.size(), 3);
        QCOMPARE(m_display->calls.last().strength, 0.0);
        service.unregisterObject(path);
    }

    void aCustomScheduleTurnsOnWithinItsHours()
    {
        m_now = at(2, 20);
        auto nightLight = start();
        QTRY_VERIFY(nightLight->available());

        nightLight->setSchedule(NightLight::Custom);
        QVERIFY(nightLight->enabled());
        QCOMPARE(nightLight->nextCheckpoint(), at(3, 6));
        QTRY_COMPARE(m_display->calls.size(), 2);
        QCOMPARE(m_display->calls.last().strength, 0.5);
        QCOMPARE(m_display->calls.last().durationMs, 1000u);

        // Moving the end before now turns it off at once.
        nightLight->setCustomEnd(19 * 60);
        QVERIFY(!nightLight->enabled());
        QCOMPARE(nightLight->nextCheckpoint(), at(3, 18));
    }

    void aScheduledTurnTakesAMinute()
    {
        m_now = at(2, 17);
        auto nightLight = start();
        nightLight->setSchedule(NightLight::Custom);
        QTRY_VERIFY(nightLight->available());
        QVERIFY(!nightLight->enabled());
        QCOMPARE(nightLight->nextCheckpoint(), at(2, 18));

        m_now = at(2, 18);
        nightLight->checkpointForTesting();
        QVERIFY(nightLight->enabled());
        QTRY_COMPARE(m_display->calls.size(), 2);
        QCOMPARE(m_display->calls.last().durationMs, 60000u);
        QCOMPARE(nightLight->nextCheckpoint(), at(3, 6));
    }

    // ScheduledFeature: the user's toggle holds until the schedule's next turn.
    void aManualToggleHoldsUntilTheNextTurn()
    {
        m_now = at(2, 20);
        auto nightLight = start();
        nightLight->setSchedule(NightLight::Custom);
        QVERIFY(nightLight->enabled());

        m_now = at(2, 21);
        nightLight->setEnabled(false);
        m_now = at(2, 23);
        nightLight->checkpointForTesting();
        QVERIFY(!nightLight->enabled());

        m_now = at(3, 6);
        nightLight->checkpointForTesting();
        QVERIFY(!nightLight->enabled());
        QCOMPARE(nightLight->nextCheckpoint(), at(3, 18));
        m_now = at(3, 18);
        nightLight->checkpointForTesting();
        QVERIFY(nightLight->enabled());
    }

    void aResumeKeepsAToggleUnlessItsTurnPassedDuringTheSleep()
    {
        m_now = at(2, 12);
        auto nightLight = start();
        nightLight->setSchedule(NightLight::Custom);
        nightLight->setEnabled(true);

        m_now = at(2, 13);
        nightLight->resumeForTesting();
        QVERIFY(nightLight->enabled());
        QCOMPARE(nightLight->nextCheckpoint(), at(2, 18));

        // Asleep through the evening and the night.
        m_now = at(3, 7);
        nightLight->resumeForTesting();
        QVERIFY(!nightLight->enabled());
        QCOMPARE(nightLight->nextCheckpoint(), at(3, 18));
    }

    void theSessionStartsWhereTheScheduleIs()
    {
        {
            QSettings settings(Preferences::file(QStringLiteral("services")), QSettings::IniFormat);
            settings.setValue(QStringLiteral("NightLight/Schedule"), int(NightLight::Custom));
            settings.setValue(QStringLiteral("NightLight/Enabled"), false);
        }
        m_now = at(2, 22);
        auto nightLight = start();
        QVERIFY(nightLight->enabled());
        QTRY_COMPARE(m_display->calls.size(), 1);
        QCOMPARE(m_display->calls.first().strength, 0.5);
    }
};

QTEST_GUILESS_MAIN(NightLightTest)
#include "night_light_test.moc"
