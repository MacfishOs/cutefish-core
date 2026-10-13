/*
    SPDX-FileCopyrightText: 2020 Reion Wong <reionwong@gmail.com>
    SPDX-FileCopyrightText: 2010 Michael Zanetti <mzanetti@kde.org>
    SPDX-FileCopyrightText: 2010 Lukas Tinkl <ltinkl@redhat.com>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "upowermanager.h"
#include "upowerdevice.h"
#include "battery.h"
#include "power.h"

#include <QDBusReply>
#include <QDebug>
#include <QDBusMetaType>
#include <QDBusConnectionInterface>

UPowerManager::UPowerManager(QObject *parent)
    : QObject(parent)
    , m_interface(UP_DBUS_SERVICE,
                  UP_DBUS_PATH,
                  UP_DBUS_INTERFACE,
                  QDBusConnection::systemBus())
    , m_onBattery(false)
    , m_lidIsPresent(false)
    , m_lidIsClosed(false)
{
    qDBusRegisterMetaType<QList<QDBusObjectPath>>();
    qDBusRegisterMetaType<QVariantMap>();

    bool serviceFound = m_interface.isValid();
    if (!serviceFound) {
        // find out whether it will be activated automatically
        QDBusMessage message = QDBusMessage::createMethodCall("org.freedesktop.DBus",
                               "/org/freedesktop/DBus",
                               "org.freedesktop.DBus",
                               "ListActivatableNames");

        QDBusReply<QStringList> reply = QDBusConnection::systemBus().call(message);
        if (reply.isValid() && reply.value().contains(UP_DBUS_SERVICE)) {
            QDBusConnection::systemBus().interface()->startService(UP_DBUS_SERVICE);
            serviceFound = true;
        }
    }

    if (serviceFound) {
        if (m_interface.metaObject()->indexOfSignal("DeviceAdded(QDBusObjectPath)") != -1) {
            connect(&m_interface, SIGNAL(DeviceAdded(QDBusObjectPath)),
                    this, SLOT(onDeviceAdded(QDBusObjectPath)));
            connect(&m_interface, SIGNAL(DeviceRemoved(QDBusObjectPath)),
                    this, SLOT(onDeviceRemoved(QDBusObjectPath)));
        }

        // On battery
        QDBusConnection::systemBus().connect(UP_DBUS_SERVICE, UP_DBUS_PATH,
                                             "org.freedesktop.DBus.Properties",
                                             "PropertiesChanged", this,
                                             SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
        m_onBattery = m_interface.property("OnBattery").toBool();
        m_lidIsPresent = m_interface.property("LidIsPresent").toBool();
        m_lidIsClosed = m_interface.property("LidIsClosed").toBool();

        // All devices
        QDBusReply<QList<QDBusObjectPath>> reply = m_interface.call("EnumerateDevices");
        if (!reply.isValid()) {
            qWarning() << Q_FUNC_INFO << " error: " << reply.error().name();
            return;
        }

        const auto pathList = reply.value();
        for (const QDBusObjectPath &path : pathList)
            addDevice(path.path());
    }
}

void UPowerManager::addDevice(const QString &path)
{
    if (m_batteries.contains(path))
        return;

    UPowerDevice *device = new UPowerDevice(path, this);
    Battery *battery = new Battery(device, this);

    if (battery->type() != Battery::PrimaryBattery) {
        device->deleteLater();
        battery->deleteLater();
        return;
    }

    device->setParent(battery);
    m_batteries.insert(path, battery);
}

bool UPowerManager::onBattery() const
{
    return m_onBattery;
}

bool UPowerManager::lidIsPresent() const
{
    return m_lidIsPresent;
}

bool UPowerManager::lidIsClosed() const
{
    return m_lidIsClosed;
}

void UPowerManager::onPropertiesChanged(const QString &ifaceName, const QVariantMap &changedProps, const QStringList &invalidatedProps)
{
    Q_UNUSED(ifaceName);

    if (changedProps.contains(QStringLiteral("OnBattery"))
        || invalidatedProps.contains(QStringLiteral("OnBattery"))) {
        const bool onBattery = m_interface.property("OnBattery").toBool();
        if (m_onBattery != onBattery) {
            m_onBattery = onBattery;
            emit onBatteryChanged();
        }
    }

    if (changedProps.contains(QStringLiteral("LidIsClosed"))
        || invalidatedProps.contains(QStringLiteral("LidIsClosed"))) {
        const bool lidIsClosed = m_interface.property("LidIsClosed").toBool();
        if (m_lidIsClosed != lidIsClosed) {
            m_lidIsClosed = lidIsClosed;
            emit lidClosedChanged();
        }
    }
}

void UPowerManager::onDeviceAdded(const QDBusObjectPath &path)
{
    addDevice(path.path());
}

void UPowerManager::onDeviceRemoved(const QDBusObjectPath &path)
{
    // Deleted now, so its D-Bus path is free for a battery added in the same batch.
    delete m_batteries.take(path.path());
    // The path it may have held goes to another primary battery.
    for (Battery *battery : std::as_const(m_batteries))
        battery->init();
}
