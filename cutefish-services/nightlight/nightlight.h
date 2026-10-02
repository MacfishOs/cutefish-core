// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NIGHTLIGHT_H
#define NIGHTLIGHT_H

#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QDateTime>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>

#include <chrono>
#include <functional>
#include <optional>

#include "displayclient.h"
#include "nightlightschedule.h"

// The night light's preferences and schedule; the compositor only animates the
// strength it is sent. Modelled on Ash's NightLightControllerImpl and
// ScheduledFeature: a change the user makes takes a second, a scheduled one a
// minute, and a manual toggle holds until the schedule's next turn.
class NightLight : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(double temperature READ temperature NOTIFY temperatureChanged)
    Q_PROPERTY(int schedule READ schedule NOTIFY scheduleChanged)
    Q_PROPERTY(int customStart READ customStart NOTIFY customStartChanged)
    Q_PROPERTY(int customEnd READ customEnd NOTIFY customEndChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
    // Ash's ScheduleType.
    enum Schedule {
        Never,
        SunsetToSunrise,
        Custom,
    };

    using Clock = std::function<QDateTime()>;

    explicit NightLight(QObject *parent = nullptr, Clock clock = [] {
        return QDateTime::currentDateTime();
    });

    bool enabled() const { return m_enabled; }

    // The strength, from 0 to 1.
    double temperature() const { return m_temperature; }

    int schedule() const { return m_schedule; }

    // Minutes after midnight.
    int customStart() const { return m_customStart; }

    int customEnd() const { return m_customEnd; }

    // Whether a display the compositor shows can be tinted.
    bool available() const { return m_available; }

    // Whether the temperature shows while the night light is off, as it does
    // while Settings' slider is held.
    bool previewing() const { return m_previewing; }

    // The schedule's next turn; invalid without a schedule.
    QDateTime nextCheckpoint() const { return m_next; }

    // What the schedule timer and a resume run, for tests that drive the clock.
    void checkpointForTesting() { advance(Reason::Scheduled); }

    void resumeForTesting() { advance(Reason::Reset); }

public slots:
    void setEnabled(bool enabled);
    void setTemperature(double temperature);
    void setSchedule(int schedule);
    void setCustomStart(int minutes);
    void setCustomEnd(int minutes);
    // Ends by itself when the caller leaves the bus.
    void setPreview(bool preview);

signals:
    void enabledChanged(bool enabled);
    void temperatureChanged(double temperature);
    void scheduleChanged(int schedule);
    void customStartChanged(int minutes);
    void customEndChanged(int minutes);
    void availableChanged(bool available);

private slots:
    void onPrepareForSleep(bool sleeping);
    void onTimeDateChanged(const QString &interface, const QVariantMap &changed,
                           const QStringList &invalidated);

private:
    // ScheduledFeature::RefreshReason.
    enum class Reason {
        // The schedule reached a turn.
        Scheduled,
        // The schedule, its times or the time zone changed, or the service started.
        SettingsChanged,
        // The system woke up.
        Reset,
    };

    void refresh(Reason reason);
    // Applies the schedule once its next turn has come; until then a manual
    // toggle holds and the timer waits for the turn.
    void advance(Reason reason);
    void arm(const QDateTime &next);
    void setEnabledInternal(bool enabled, std::chrono::milliseconds duration);
    void endPreview();
    void sendToCompositor(std::chrono::milliseconds duration);
    void updateAvailable();
    NightLightSchedule::Times timesFor(QDate date);
    void write(const QString &key, const QVariant &value);

    QSettings m_settings;
    DisplayClient m_display;
    Clock m_clock;
    QTimer m_timer;
    QDBusServiceWatcher m_previewWatcher;
    QDateTime m_next;
    bool m_enabled = false;
    double m_temperature = 0.5;
    int m_schedule = Never;
    int m_customStart = 18 * 60;
    int m_customEnd = 6 * 60;
    bool m_available = false;
    bool m_previewing = false;
    QByteArray m_zone;
    std::optional<NightLightSchedule::Coordinates> m_coordinates;
};

#endif // NIGHTLIGHT_H
