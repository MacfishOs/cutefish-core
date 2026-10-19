// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#ifndef BATTERY_H
#define BATTERY_H

#include <cstdint>
#include <memory>
#include <string>

#include "base/dbus.h"
#include "base/settings.h"

// One UPower device, published on the session bus when it powers the computer.
class Battery
{
public:
    // The chargeState values of com.cutefish.Services.Battery.
    enum ChargeState { NoCharge, Charging, Discharging, FullyCharged };

    Battery(cutefish::base::dbus::Connection *system, const std::string &path,
            cutefish::base::Settings &settings);
    ~Battery();

    Battery(const Battery &) = delete;
    Battery &operator=(const Battery &) = delete;

    // A battery of the computer, not of a mouse, a phone or a UPS.
    bool powersComputer() const;

    // com.cutefish.Services.Battery at /com/cutefish/Services/Battery; one
    // battery at a time holds the path.
    bool publish(cutefish::base::dbus::Connection *session);

    // "2h 5m"; empty under a minute.
    static std::string formatDuration(int64_t seconds);

private:
    struct Reading {
        uint32_t type = 0;
        uint32_t state = 0;
        int percent = 0;
        int capacity = 0;
        double voltage = 0;
        int64_t timeToEmpty = 0;
        int64_t timeToFull = 0;
        bool thresholdEnabled = false;

        ChargeState chargeState() const;
        // Seconds to full while charging, to empty while discharging, else -1.
        int64_t remainingTime() const;
        std::string status() const;
        bool operator==(const Reading &) const = default;
    };

    Reading read() const;
    void update();
    std::string lastChargedTime() const;

    std::string m_path;
    cutefish::base::Settings &m_settings;
    cutefish::base::dbus::Proxy m_device;
    cutefish::base::dbus::Match m_propertiesChanged;
    cutefish::base::dbus::Match m_prepareForSleep;
    std::unique_ptr<cutefish::base::dbus::Object> m_object;
    Reading m_reading;

    int m_lastChargedPercent = 0;
    uint64_t m_lastChargedSecs = 0;
};

#endif // BATTERY_H
