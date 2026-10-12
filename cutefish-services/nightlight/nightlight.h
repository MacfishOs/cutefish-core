// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NIGHTLIGHT_H
#define NIGHTLIGHT_H

#include <QDBusContext>
#include <QDBusServiceWatcher>

#include <chrono>

#include "displayclient.h"
#include "scheduledfeature.h"

// The night light's preferences and schedule; the compositor only animates the
// strength it is sent. Modelled on Ash's NightLightControllerImpl: a change the
// user makes takes a second, a scheduled one a minute.
class NightLight : public ScheduledFeature, protected QDBusContext
{
    Q_OBJECT
    Q_PROPERTY(double temperature READ temperature NOTIFY temperatureChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
    explicit NightLight(QObject *parent = nullptr, Clock clock = [] {
        return QDateTime::currentDateTime();
    });

    // The strength, from 0 to 1.
    double temperature() const { return m_temperature; }

    // Whether a display the compositor shows can be tinted.
    bool available() const { return m_available; }

    // Whether the temperature shows while the night light is off, as it does
    // while Settings' slider is held.
    bool previewing() const { return m_previewing; }

public slots:
    void setTemperature(double temperature);
    // Ends by itself when the caller leaves the bus.
    void setPreview(bool preview);

signals:
    void temperatureChanged(double temperature);
    void availableChanged(bool available);

protected:
    void showEnabled(std::chrono::milliseconds duration) override;

private:
    void endPreview();
    void sendToCompositor(std::chrono::milliseconds duration);
    void updateAvailable();

    DisplayClient m_display;
    QDBusServiceWatcher m_previewWatcher;
    double m_temperature = 0.5;
    bool m_available = false;
    bool m_previewing = false;
};

#endif // NIGHTLIGHT_H
