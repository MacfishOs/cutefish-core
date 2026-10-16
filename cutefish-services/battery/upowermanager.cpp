/*
    SPDX-FileCopyrightText: 2020 Reion Wong <reionwong@gmail.com>
    SPDX-FileCopyrightText: 2010 Michael Zanetti <mzanetti@kde.org>
    SPDX-FileCopyrightText: 2010 Lukas Tinkl <ltinkl@redhat.com>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "upowermanager.h"
#include "battery.h"
#include "power.h"
#include "upowerdevice.h"

#include <algorithm>

#include "log.h"

using namespace cutefish::base;

UPowerManager::UPowerManager(dbus::Connection *system, dbus::Connection *session, Settings &settings)
    : m_session(session)
    , m_settings(settings)
    , m_interface(system, UP_DBUS_SERVICE, UP_DBUS_PATH, UP_DBUS_INTERFACE)
{
    if (!system)
        return;

    m_matches.push_back(m_interface.onSignal("DeviceAdded", [this](const dbus::Message &message) {
        if (const auto path = message.argument<dbus::ObjectPath>())
            addDevice(path->path);
    }));
    m_matches.push_back(m_interface.onSignal("DeviceRemoved", [this](const dbus::Message &message) {
        if (const auto path = message.argument<dbus::ObjectPath>())
            removeDevice(path->path);
    }));
    m_matches.push_back(m_interface.onPropertiesChanged(
            [this](const dbus::VariantMap &changed, const std::vector<std::string> &invalidated) {
                onPropertiesChanged(changed, invalidated);
            }));

    // The call starts UPower when the bus can activate it.
    const dbus::Reply properties = m_interface.getAll();
    if (properties.isError())
        return;
    const dbus::VariantMap values = properties.value<dbus::VariantMap>().value_or(dbus::VariantMap());
    const auto flag = [&values](const char *name) {
        const auto it = values.find(name);
        return it != values.end() && it->second.get<bool>().value_or(false);
    };
    m_onBattery = flag("OnBattery");
    m_lidIsPresent = flag("LidIsPresent");
    m_lidIsClosed = flag("LidIsClosed");

    // All devices
    const dbus::Reply reply = m_interface.call("EnumerateDevices");
    const auto paths = reply.value<std::vector<dbus::ObjectPath>>();
    if (!paths) {
        CF_WARNING(services::log()) << "UPower EnumerateDevices failed:" << reply.error().name;
        return;
    }

    for (const dbus::ObjectPath &path : *paths)
        addDevice(path.path);
}

UPowerManager::~UPowerManager() = default;

void UPowerManager::addDevice(const std::string &path)
{
    if (m_batteries.contains(path))
        return;

    auto battery = std::make_unique<Battery>(
            std::make_unique<UPowerDevice>(m_interface.connection(), path), m_settings);

    if (battery->type() != Battery::PrimaryBattery)
        return;

    m_batteries.emplace(path, std::move(battery));
    publishBattery();
}

void UPowerManager::removeDevice(const std::string &path)
{
    const auto it = m_batteries.find(path);
    if (it == m_batteries.end())
        return;

    // Gone now, so its D-Bus path is free for a battery added in the same batch.
    if (m_published == it->second.get())
        m_published = nullptr;
    m_batteries.erase(it);
    // The path it may have held goes to another primary battery.
    publishBattery();
}

void UPowerManager::publishBattery()
{
    if (m_published || !m_session)
        return;

    for (auto &[path, battery] : m_batteries) {
        if (battery->publish(m_session)) {
            m_published = battery.get();
            return;
        }
    }
}

void UPowerManager::onPropertiesChanged(const dbus::VariantMap &changed,
                                        const std::vector<std::string> &invalidated)
{
    const auto touched = [&](const std::string &name) {
        return changed.contains(name)
                || std::find(invalidated.begin(), invalidated.end(), name) != invalidated.end();
    };
    const auto read = [this](const std::string &name) {
        return m_interface.property<bool>(name).value_or(false);
    };

    if (touched("OnBattery")) {
        const bool onBattery = read("OnBattery");
        if (m_onBattery != onBattery) {
            m_onBattery = onBattery;
            onBatteryChanged.emit();
        }
    }

    if (touched("LidIsClosed")) {
        const bool lidIsClosed = read("LidIsClosed");
        if (m_lidIsClosed != lidIsClosed) {
            m_lidIsClosed = lidIsClosed;
            lidClosedChanged.emit();
        }
    }
}
