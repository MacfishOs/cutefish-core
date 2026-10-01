// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nightlight.h"

#include <QDBusConnection>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

#include "preferences.h"

using namespace std::chrono_literals;

namespace {

constexpr auto s_group = "NightLight";

// kManualAnimationDuration and kAutomaticAnimationDuration.
constexpr std::chrono::milliseconds kManualDuration = 1s;
constexpr std::chrono::milliseconds kScheduledDuration = 60s;

// Ash's defaults: a custom schedule from 6 PM to 6 AM, and the same times for
// sunset and sunrise where the location is unknown or the sun does not set.
constexpr int kDefaultStart = 18 * 60;
constexpr int kDefaultEnd = 6 * 60;
constexpr double kDefaultTemperature = 0.5;

constexpr int kMinutesPerDay = 24 * 60;

QTime timeOfDay(int minutes)
{
    return QTime(0, 0).addSecs(minutes * 60);
}

int minutesOfDay(int minutes)
{
    return (minutes % kMinutesPerDay + kMinutesPerDay) % kMinutesPerDay;
}

} // namespace

NightLight::NightLight(QObject *parent, Clock clock)
    : QObject(parent)
    , m_settings(Preferences::file(QStringLiteral("services")), QSettings::IniFormat)
    , m_clock(std::move(clock))
{
    m_settings.beginGroup(QLatin1String(s_group));
    m_enabled = m_settings.value(QStringLiteral("Enabled"), false).toBool();
    const double temperature =
            m_settings.value(QStringLiteral("Temperature"), kDefaultTemperature).toDouble();
    m_temperature = std::isfinite(temperature) ? std::clamp(temperature, 0.0, 1.0)
                                               : kDefaultTemperature;
    m_schedule = std::clamp(m_settings.value(QStringLiteral("Schedule"), int(Never)).toInt(),
                            int(Never), int(Custom));
    m_customStart =
            minutesOfDay(m_settings.value(QStringLiteral("CustomStart"), kDefaultStart).toInt());
    m_customEnd = minutesOfDay(m_settings.value(QStringLiteral("CustomEnd"), kDefaultEnd).toInt());
    m_settings.endGroup();

    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        advance(Reason::Scheduled);
    });

    connect(&m_display, &DisplayClient::stateChanged, this, &NightLight::updateAvailable);
    // A compositor that starts, or starts again, shows no tint until told.
    connect(&m_display, &DisplayClient::availabilityChanged, this, [this] {
        sendToCompositor(kManualDuration);
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

    // The schedule decides how a session starts.
    refresh(Reason::SettingsChanged);
}

void NightLight::setEnabled(bool enabled)
{
    // The schedule keeps its turn: a manual toggle holds until then.
    setEnabledInternal(enabled, kManualDuration);
}

void NightLight::setTemperature(double temperature)
{
    if (!std::isfinite(temperature))
        return;
    temperature = std::clamp(temperature, 0.0, 1.0);
    if (temperature == m_temperature)
        return;
    m_temperature = temperature;
    write(QStringLiteral("Temperature"), temperature);
    if (m_enabled)
        sendToCompositor(kManualDuration);
    emit temperatureChanged(temperature);
}

void NightLight::setSchedule(int schedule)
{
    if (schedule < Never || schedule > Custom || schedule == m_schedule)
        return;
    m_schedule = schedule;
    write(QStringLiteral("Schedule"), schedule);
    emit scheduleChanged(schedule);
    refresh(Reason::SettingsChanged);
}

void NightLight::setCustomStart(int minutes)
{
    minutes = minutesOfDay(minutes);
    if (minutes == m_customStart)
        return;
    m_customStart = minutes;
    write(QStringLiteral("CustomStart"), minutes);
    emit customStartChanged(minutes);
    if (m_schedule == Custom)
        refresh(Reason::SettingsChanged);
}

void NightLight::setCustomEnd(int minutes)
{
    minutes = minutesOfDay(minutes);
    if (minutes == m_customEnd)
        return;
    m_customEnd = minutes;
    write(QStringLiteral("CustomEnd"), minutes);
    emit customEndChanged(minutes);
    if (m_schedule == Custom)
        refresh(Reason::SettingsChanged);
}

void NightLight::onPrepareForSleep(bool sleeping)
{
    if (!sleeping)
        advance(Reason::Reset);
}

void NightLight::onTimeDateChanged(const QString &, const QVariantMap &changed,
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
void NightLight::refresh(Reason reason)
{
    if (m_schedule == Never) {
        arm({});
        return;
    }
    const auto position = NightLightSchedule::position(m_clock(), [this](QDate date) {
        return timesFor(date);
    });
    if (position.enabled != m_enabled)
        setEnabledInternal(position.enabled,
                           reason == Reason::Scheduled ? kScheduledDuration : kManualDuration);
    arm(position.next);
}

// ScheduledFeature::SuspendDone keeps a manual toggle through the sleep unless
// the turn it waited for passed meanwhile. A timer that fires before its turn,
// the clock having been set back, waits for it the same way.
void NightLight::advance(Reason reason)
{
    if (m_next.isValid() && m_clock() < m_next) {
        arm(m_next);
        return;
    }
    refresh(reason);
}

void NightLight::arm(const QDateTime &next)
{
    m_next = next;
    if (!next.isValid()) {
        m_timer.stop();
        return;
    }
    m_timer.start(std::chrono::milliseconds(std::max<qint64>(m_clock().msecsTo(next), 0)));
}

void NightLight::setEnabledInternal(bool enabled, std::chrono::milliseconds duration)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    write(QStringLiteral("Enabled"), enabled);
    sendToCompositor(duration);
    emit enabledChanged(enabled);
}

void NightLight::sendToCompositor(std::chrono::milliseconds duration)
{
    // Until its state has loaded, availabilityChanged sends it all.
    if (m_display.available())
        m_display.setNightLight(m_enabled ? m_temperature : 0.0, uint(duration.count()));
}

void NightLight::updateAvailable()
{
    const bool available = m_display.nightLightAvailable();
    if (available == m_available)
        return;
    m_available = available;
    emit availableChanged(available);
}

NightLightSchedule::Times NightLight::timesFor(QDate date)
{
    if (m_schedule == Custom)
        return {timeOfDay(m_customStart), timeOfDay(m_customEnd)};

    const QByteArray zone = QTimeZone::systemTimeZoneId();
    if (zone != m_zone) {
        m_zone = zone;
        m_coordinates = NightLightSchedule::zoneCoordinates(zone);
    }
    if (m_coordinates) {
        const auto sun = NightLightSchedule::sunTimes(date, m_coordinates->latitude,
                                                      m_coordinates->longitude,
                                                      QTimeZone::systemTimeZone());
        if (sun)
            return {sun->sunset.time(), sun->sunrise.time()};
    }
    return {timeOfDay(kDefaultStart), timeOfDay(kDefaultEnd)};
}

void NightLight::write(const QString &key, const QVariant &value)
{
    m_settings.beginGroup(QLatin1String(s_group));
    m_settings.setValue(key, value);
    m_settings.endGroup();
    m_settings.sync();
}
