// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nightlight.h"

#include <QDBusConnection>

#include <algorithm>
#include <cmath>

using namespace std::chrono_literals;

// kManualAnimationDuration.
static constexpr std::chrono::milliseconds kManualDuration = 1s;
// Settings sends a held slider's value every 100 ms; a second would trail it.
static constexpr std::chrono::milliseconds kPreviewDuration = 100ms;

static constexpr double kDefaultTemperature = 0.5;

NightLight::NightLight(QObject *parent, Clock clock)
    : ScheduledFeature({QStringLiteral("NightLight"), QStringLiteral("Enabled"),
                        QStringLiteral("Schedule"), QStringLiteral("HoldUntil"),
                        QStringLiteral("CustomStart"), QStringLiteral("CustomEnd")},
                       parent, std::move(clock))
{
    const double temperature =
            m_settings.value(QStringLiteral("NightLight/Temperature"), kDefaultTemperature)
                    .toDouble();
    m_temperature = std::isfinite(temperature) ? std::clamp(temperature, 0.0, 1.0)
                                               : kDefaultTemperature;

    m_previewWatcher.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(&m_previewWatcher, &QDBusServiceWatcher::serviceUnregistered, this,
            &NightLight::endPreview);

    connect(&m_display, &DisplayClient::stateChanged, this, &NightLight::updateAvailable);
    // A compositor that starts, or starts again, shows no tint until told.
    connect(&m_display, &DisplayClient::availabilityChanged, this, [this] {
        sendToCompositor(kManualDuration);
    });

    start();
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
    if (m_previewing)
        sendToCompositor(kPreviewDuration);
    else if (enabled())
        sendToCompositor(kManualDuration);
    emit temperatureChanged(temperature);
}

void NightLight::setPreview(bool preview)
{
    if (!preview) {
        endPreview();
        return;
    }
    // A caller that crashes mid-drag must not leave the screen tinted.
    if (calledFromDBus()) {
        m_previewWatcher.setConnection(connection());
        m_previewWatcher.setWatchedServices({message().service()});
    }
    if (m_previewing)
        return;
    m_previewing = true;
    if (!enabled())
        sendToCompositor(kManualDuration);
}

void NightLight::showEnabled(std::chrono::milliseconds duration)
{
    sendToCompositor(duration);
}

void NightLight::endPreview()
{
    m_previewWatcher.setWatchedServices({});
    if (!m_previewing)
        return;
    m_previewing = false;
    if (!enabled())
        sendToCompositor(kManualDuration);
}

void NightLight::sendToCompositor(std::chrono::milliseconds duration)
{
    // Until its state has loaded, availabilityChanged sends it all.
    if (m_display.available())
        m_display.setNightLight(enabled() || m_previewing ? m_temperature : 0.0,
                                uint(duration.count()));
}

void NightLight::updateAvailable()
{
    const bool available = m_display.nightLightAvailable();
    if (available == m_available)
        return;
    m_available = available;
    emit availableChanged(available);
}
