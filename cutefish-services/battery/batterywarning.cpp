// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "batterywarning.h"
#include "battery.h"
#include "upower.h"

#include <cmath>
#include <format>

using namespace cutefish::base;

static const char displayDevice[] = "/org/freedesktop/UPower/devices/DisplayDevice";

static int64_t integer(const dbus::Variant &value)
{
    if (const auto number = value.get<int64_t>())
        return *number;
    if (const auto number = value.get<double>())
        return int64_t(*number);
    return 0;
}

BatteryWarning::BatteryWarning(dbus::Connection *system, dbus::Connection *session)
    : m_upower(system, upower::service, upower::path, upower::interface)
    , m_device(system, upower::service, displayDevice, upower::deviceInterface)
    , m_notification(session)
{
    m_notification.setDesktopEntry("cutefish-settings");
    m_notification.setIconName("battery-caution");
    // No notification server yet, as early in a session: the next battery change retries.
    m_failed = m_notification.failed.connect([this] {
        m_shownLevel = None;
    });

    if (!system)
        return;

    m_changed = m_device.onPropertiesChanged(
            [this](const dbus::VariantMap &changed, const std::vector<std::string> &invalidated) {
                onPropertiesChanged(changed, invalidated);
            });

    fetchProperties();
}

BatteryWarning::~BatteryWarning() = default;

void BatteryWarning::onPropertiesChanged(const dbus::VariantMap &changed,
                                         const std::vector<std::string> &invalidated)
{
    if (!invalidated.empty()) {
        fetchProperties();
        return;
    }

    for (const auto &[name, value] : changed)
        m_properties[name] = value;
    update();
}

void BatteryWarning::fetchProperties()
{
    m_propertiesCall = m_device.getAllAsync([this](const dbus::Reply &reply) {
        const auto properties = reply.value<dbus::VariantMap>();
        if (!properties)
            return;

        m_properties = *properties;
        update();
    });
}

void BatteryWarning::fetchCriticalAction()
{
    m_fetchingAction = true;

    m_actionCall = m_upower.callAsync("GetCriticalAction", [this](const dbus::Reply &reply) {
        m_criticalAction = reply.value<std::string>().value_or(std::string());
        m_criticalActionKnown = true;
        m_fetchingAction = false;
        update();
    });
}

void BatteryWarning::update()
{
    if (m_fetchingAction)
        return;

    const auto level = m_properties.find("WarningLevel");
    const uint32_t warningLevel =
            level == m_properties.end() ? 0 : level->second.get<uint32_t>().value_or(0);

    if (warningLevel < Low) {
        if (m_shownLevel != None)
            m_notification.close();

        m_shownLevel = None;
        // logind may allow hibernation by the next discharge.
        m_criticalActionKnown = false;
        return;
    }

    // Each level is announced once per discharge; a dismissed warning stays dismissed,
    // and the next level after it is a banner of its own.
    if (warningLevel <= m_shownLevel)
        return;

    if (warningLevel >= Critical && !m_criticalActionKnown) {
        fetchCriticalAction();
        return;
    }

    notify(warningLevel);
}

void BatteryWarning::notify(uint32_t level)
{
    if (level == Low)
        m_notification.setSummary("Low Battery");
    else if (level == Critical)
        m_notification.setSummary("Battery Critically Low");
    else
        m_notification.setSummary("Battery Empty");

    m_notification.setBody(body(level));
    m_notification.setUrgency(level == Low ? Notification::Normal : Notification::Critical);
    m_shownLevel = level;
    m_notification.show();
}

std::string BatteryWarning::body(uint32_t level) const
{
    if (level == Low) {
        const auto percentage = m_properties.find("Percentage");
        const auto timeToEmpty = m_properties.find("TimeToEmpty");
        const int percent = percentage == m_properties.end()
                ? 0
                : int(std::lround(percentage->second.get<double>().value_or(0)));
        const int64_t time = timeToEmpty == m_properties.end() ? 0 : integer(timeToEmpty->second);

        // UPower reports 0 until it trusts the discharge rate.
        if (time >= 60)
            return std::format("{}% of battery remaining ({}).", percent, Battery::formatDuration(time));

        return std::format("{}% of battery remaining.", percent);
    }

    const bool powerOff = m_criticalAction == "PowerOff";
    const bool hibernate = m_criticalAction == "Hibernate" || m_criticalAction == "HybridSleep";
    const bool suspend = m_criticalAction == "Suspend";

    if (level == Critical) {
        if (powerOff)
            return "Save your work and connect to power. The computer will shut down when the battery is nearly empty.";
        if (hibernate)
            return "Connect to power. The computer will hibernate when the battery is nearly empty.";
        if (suspend)
            return "Save your work and connect to power. The computer will go to sleep when the battery is nearly empty.";

        return "Connect to power soon.";
    }

    // UPower waits 20 seconds before the action and drops it once power returns.
    if (powerOff)
        return "The computer is about to shut down. Connect to power to keep it running.";
    if (hibernate)
        return "The computer is about to hibernate. Connect to power to keep it running.";
    if (suspend)
        return "The computer is about to go to sleep. Connect to power to keep it running.";

    return "Connect to power now.";
}
