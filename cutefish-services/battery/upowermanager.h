/*
    SPDX-FileCopyrightText: 2020 Reion Wong <reionwong@gmail.com>
    SPDX-FileCopyrightText: 2010 Michael Zanetti <mzanetti@kde.org>
    SPDX-FileCopyrightText: 2010 Lukas Tinkl <ltinkl@redhat.com>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#ifndef UPOWERMANAGER_H
#define UPOWERMANAGER_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/settings.h"
#include "base/signal.h"

class Battery;

class UPowerManager
{
public:
    // UPower on `system`; the primary battery is published on `session`.
    UPowerManager(cutefish::base::dbus::Connection *system, cutefish::base::dbus::Connection *session,
                  cutefish::base::Settings &settings);
    ~UPowerManager();

    UPowerManager(const UPowerManager &) = delete;
    UPowerManager &operator=(const UPowerManager &) = delete;

    bool onBattery() const { return m_onBattery; }
    bool lidIsPresent() const { return m_lidIsPresent; }
    bool lidIsClosed() const { return m_lidIsClosed; }

    cutefish::base::Signal<> onBatteryChanged;
    cutefish::base::Signal<> lidClosedChanged;

private:
    void onPropertiesChanged(const cutefish::base::dbus::VariantMap &changed,
                             const std::vector<std::string> &invalidated);
    void addDevice(const std::string &path);
    void removeDevice(const std::string &path);
    // Gives the D-Bus path to a primary battery when none holds it.
    void publishBattery();

    cutefish::base::dbus::Connection *m_session;
    cutefish::base::Settings &m_settings;
    cutefish::base::dbus::Proxy m_interface;
    std::vector<cutefish::base::dbus::Match> m_matches;
    // Primary batteries by UPower device path.
    std::map<std::string, std::unique_ptr<Battery>> m_batteries;
    // The battery on /com/cutefish/Services/Battery.
    Battery *m_published = nullptr;
    bool m_onBattery = false;
    bool m_lidIsPresent = false;
    bool m_lidIsClosed = false;
};

#endif // UPOWERMANAGER_H
