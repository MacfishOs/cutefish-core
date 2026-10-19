// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef POWERSUPPLY_H
#define POWERSUPPLY_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/settings.h"
#include "base/signal.h"

class Battery;

// What UPower knows of the power source and the lid, and the computer's batteries.
class PowerSupply
{
public:
    // UPower on `system`; the battery that holds the path is published on `session`.
    PowerSupply(cutefish::base::dbus::Connection *system, cutefish::base::dbus::Connection *session,
                cutefish::base::Settings &settings);
    ~PowerSupply();

    PowerSupply(const PowerSupply &) = delete;
    PowerSupply &operator=(const PowerSupply &) = delete;

    bool onBattery() const { return m_onBattery; }
    bool hasLid() const { return m_hasLid; }
    bool lidClosed() const { return m_lidClosed; }

    cutefish::base::Signal<> powerSourceChanged;
    cutefish::base::Signal<> lidChanged;

private:
    void readFlags(const cutefish::base::dbus::VariantMap &values);
    void addBattery(const std::string &path);
    void removeBattery(const std::string &path);
    void publishBattery();

    cutefish::base::dbus::Connection *m_system;
    cutefish::base::dbus::Connection *m_session;
    cutefish::base::Settings &m_settings;
    cutefish::base::dbus::Proxy m_upower;
    std::vector<cutefish::base::dbus::Match> m_matches;
    // By UPower device path.
    std::map<std::string, std::unique_ptr<Battery>> m_batteries;
    Battery *m_published = nullptr;
    bool m_onBattery = false;
    bool m_hasLid = false;
    bool m_lidClosed = false;
};

#endif // POWERSUPPLY_H
