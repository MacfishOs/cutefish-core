// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scheduledfeature.h"

#include <QDBusConnection>
#include <QTimeZone>

#include <algorithm>

#include "preferences.h"

using namespace std::chrono_literals;

// kManualAnimationDuration and kAutomaticAnimationDuration.
static constexpr std::chrono::milliseconds kManualDuration = 1s;
static constexpr std::chrono::milliseconds kScheduledDuration = 60s;

// Ash's defaults: a custom schedule from 6 PM to 6 AM, and the same times for
// sunset and sunrise where the location is unknown or the sun does not set.
static constexpr int kDefaultStart = 18 * 60;
static constexpr int kDefaultEnd = 6 * 60;

static constexpr int kMinutesPerDay = 24 * 60;

static QTime timeOfDay(int minutes)
{
    return QTime(0, 0).addSecs(minutes * 60);
}

static int minutesOfDay(int minutes)
{
    return (minutes % kMinutesPerDay + kMinutesPerDay) % kMinutesPerDay;
}

static int minutesOfDay(QTime time)
{
    return time.hour() * 60 + time.minute();
}

ScheduledFeature::ScheduledFeature(const Keys &keys, QObject *parent, Clock clock)
    : QObject(parent)
    , m_settings(Preferences::file(QStringLiteral("services")), QSettings::IniFormat)
    , m_keys(keys)
    , m_clock(std::move(clock))
{
    m_settings.beginGroup(m_keys.group);
    m_enabled = m_settings.value(m_keys.enabled, false).toBool();
    m_schedule = std::clamp(m_settings.value(m_keys.schedule, int(Never)).toInt(), int(Never),
                            int(hasCustomSchedule() ? Custom : SunsetToSunrise));
    if (hasCustomSchedule()) {
        m_customStart = minutesOfDay(m_settings.value(m_keys.customStart, kDefaultStart).toInt());
        m_customEnd = minutesOfDay(m_settings.value(m_keys.customEnd, kDefaultEnd).toInt());
    }
    m_settings.endGroup();

    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        advance(Reason::Scheduled);
    });

    // Timers do not run while the system sleeps, and the time zone moves every turn.
    QDBusConnection::systemBus().connect(
            QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
            QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("PrepareForSleep"),
            this, SLOT(onPrepareForSleep(bool)));
    QDBusConnection::systemBus().connect(
            QStringLiteral("org.freedesktop.timedate1"), QStringLiteral("/org/freedesktop/timedate1"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
            this, SLOT(onTimeDateChanged(QString, QVariantMap, QStringList)));
}

void ScheduledFeature::start()
{
    // The schedule decides how a session starts, unless a manual toggle from
    // an earlier session still waits for its turn.
    const QVariant hold = m_settings.value(holdPath());
    if (m_schedule != Never && hold.isValid()
        && m_clock().toMSecsSinceEpoch() < hold.toLongLong())
        arm(QDateTime::fromMSecsSinceEpoch(hold.toLongLong()));
    else
        refresh(Reason::SettingsChanged);
}

void ScheduledFeature::setEnabled(bool enabled)
{
    // The schedule keeps its turn: a manual toggle holds until then, across
    // sessions too.
    if (m_next.isValid() && enabled != m_enabled)
        write(m_keys.holdUntil, m_next.toMSecsSinceEpoch());
    setEnabledInternal(enabled, kManualDuration);
}

void ScheduledFeature::setSchedule(int schedule)
{
    if (schedule < Never || schedule > (hasCustomSchedule() ? Custom : SunsetToSunrise)
        || schedule == m_schedule)
        return;
    m_schedule = schedule;
    write(m_keys.schedule, schedule);
    emit scheduleChanged(schedule);
    refresh(Reason::SettingsChanged);
}

void ScheduledFeature::setCustomStart(int minutes)
{
    minutes = minutesOfDay(minutes);
    if (!hasCustomSchedule() || minutes == m_customStart)
        return;
    m_customStart = minutes;
    write(m_keys.customStart, minutes);
    emit customStartChanged(minutes);
    if (m_schedule == Custom)
        refresh(Reason::SettingsChanged);
}

void ScheduledFeature::setCustomEnd(int minutes)
{
    minutes = minutesOfDay(minutes);
    if (!hasCustomSchedule() || minutes == m_customEnd)
        return;
    m_customEnd = minutes;
    write(m_keys.customEnd, minutes);
    emit customEndChanged(minutes);
    if (m_schedule == Custom)
        refresh(Reason::SettingsChanged);
}

void ScheduledFeature::showEnabled(std::chrono::milliseconds)
{
}

void ScheduledFeature::onPrepareForSleep(bool sleeping)
{
    if (!sleeping)
        advance(Reason::Reset);
}

void ScheduledFeature::onTimeDateChanged(const QString &, const QVariantMap &changed,
                                         const QStringList &invalidated)
{
    const QString timezone = QStringLiteral("Timezone");
    if (!changed.contains(timezone) && !invalidated.contains(timezone))
        return;
    m_zone.clear();
    refresh(Reason::SettingsChanged);
}

// ScheduledFeature::RefreshScheduleTimer: where the schedule says otherwise,
// it wins; a change of settings takes a second to show, a scheduled turn a minute.
void ScheduledFeature::refresh(Reason reason)
{
    if (m_settings.contains(holdPath())) {
        m_settings.remove(holdPath());
        m_settings.sync();
    }
    updateSunTimes();
    if (m_schedule == Never) {
        arm({});
        return;
    }
    const auto position = ScheduleUtils::position(m_clock(), [this](QDate date) {
        return timesFor(date);
    });
    setEnabledInternal(position.enabled,
                       reason == Reason::Scheduled ? kScheduledDuration : kManualDuration);
    arm(position.next);
}

// ScheduledFeature::SuspendDone keeps a manual toggle through the sleep unless
// the turn it waited for passed meanwhile. A timer that fires before its turn,
// the clock having been set back, waits for it the same way.
void ScheduledFeature::advance(Reason reason)
{
    if (m_next.isValid() && m_clock() < m_next) {
        arm(m_next);
        return;
    }
    refresh(reason);
}

void ScheduledFeature::arm(const QDateTime &next)
{
    m_next = next;
    if (!next.isValid()) {
        m_timer.stop();
        return;
    }
    m_timer.start(std::chrono::milliseconds(std::max<qint64>(m_clock().msecsTo(next), 0)));
}

void ScheduledFeature::setEnabledInternal(bool enabled, std::chrono::milliseconds duration)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    write(m_keys.enabled, enabled);
    showEnabled(duration);
    emit enabledChanged(enabled);
}

void ScheduledFeature::updateSunTimes()
{
    const QDateTime now = m_clock();
    const ScheduleUtils::Times today = sunTimesFor(now.date());
    const ScheduleUtils::Times tomorrow = sunTimesFor(now.date().addDays(1));
    const int sunset = minutesOfDay(today.start > now.time() ? today.start : tomorrow.start);
    const int sunrise = minutesOfDay(today.end > now.time() ? today.end : tomorrow.end);
    if (sunset != m_sunset) {
        m_sunset = sunset;
        emit sunsetChanged(sunset);
    }
    if (sunrise != m_sunrise) {
        m_sunrise = sunrise;
        emit sunriseChanged(sunrise);
    }
}

ScheduleUtils::Times ScheduledFeature::timesFor(QDate date)
{
    if (m_schedule == Custom)
        return {timeOfDay(m_customStart), timeOfDay(m_customEnd)};
    return sunTimesFor(date);
}

ScheduleUtils::Times ScheduledFeature::sunTimesFor(QDate date)
{
    const QByteArray zone = QTimeZone::systemTimeZoneId();
    if (zone != m_zone) {
        m_zone = zone;
        m_coordinates = ScheduleUtils::zoneCoordinates(zone);
    }
    if (m_coordinates) {
        const auto sun = ScheduleUtils::sunTimes(date, m_coordinates->latitude,
                                                 m_coordinates->longitude,
                                                 QTimeZone::systemTimeZone());
        if (sun)
            return {sun->sunset.time(), sun->sunrise.time()};
    }
    return {timeOfDay(kDefaultStart), timeOfDay(kDefaultEnd)};
}

void ScheduledFeature::write(const QString &key, const QVariant &value)
{
    m_settings.beginGroup(m_keys.group);
    m_settings.setValue(key, value);
    m_settings.endGroup();
    m_settings.sync();
}
