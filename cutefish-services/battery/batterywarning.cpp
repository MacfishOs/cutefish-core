// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "batterywarning.h"
#include "battery.h"
#include "cutefishnotification.h"
#include "power.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace {
const QString displayDevice = QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice");
}

BatteryWarning::BatteryWarning(QObject *parent)
    : QObject(parent)
    , m_notification(new Cutefish::Notification(this))
    , m_criticalActionKnown(false)
    , m_fetchingAction(false)
    , m_shownLevel(None)
{
    m_notification->setDesktopEntry(QStringLiteral("cutefish-settings"));
    m_notification->setIconName(QStringLiteral("battery-caution"));
    // No notification server yet, as early in a session: the next battery change retries.
    connect(m_notification, &Cutefish::Notification::failed, this, [this] {
        m_shownLevel = None;
    });

    QDBusConnection::systemBus().connect(UP_DBUS_SERVICE, displayDevice,
                                         QStringLiteral("org.freedesktop.DBus.Properties"),
                                         QStringLiteral("PropertiesChanged"), this,
                                         SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));

    fetchProperties();
}

void BatteryWarning::onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                         const QStringList &invalidated)
{
    if (interface != QLatin1String(UP_DBUS_INTERFACE_DEVICE))
        return;

    if (!invalidated.isEmpty()) {
        fetchProperties();
        return;
    }

    m_properties.insert(changed);
    update();
}

void BatteryWarning::fetchProperties()
{
    QDBusMessage message = QDBusMessage::createMethodCall(UP_DBUS_SERVICE, displayDevice,
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    message << QStringLiteral(UP_DBUS_INTERFACE_DEVICE);

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();

        QDBusPendingReply<QVariantMap> reply = *watcher;
        if (reply.isError())
            return;

        m_properties = reply.value();
        update();
    });
}

void BatteryWarning::fetchCriticalAction()
{
    QDBusMessage message = QDBusMessage::createMethodCall(UP_DBUS_SERVICE, UP_DBUS_PATH,
            UP_DBUS_INTERFACE, QStringLiteral("GetCriticalAction"));

    m_fetchingAction = true;

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();

        QDBusPendingReply<QString> reply = *watcher;
        m_criticalAction = reply.isValid() ? reply.value() : QString();
        m_criticalActionKnown = true;
        m_fetchingAction = false;
        update();
    });
}

void BatteryWarning::update()
{
    if (m_fetchingAction)
        return;

    const uint level = m_properties.value(QStringLiteral("WarningLevel")).toUInt();

    if (level < Low) {
        if (m_shownLevel != None)
            m_notification->close();

        m_shownLevel = None;
        // logind may allow hibernation by the next discharge.
        m_criticalActionKnown = false;
        return;
    }

    // Each level is announced once per discharge; a dismissed warning stays dismissed,
    // and the next level after it is a banner of its own.
    if (level <= m_shownLevel)
        return;

    if (level >= Critical && !m_criticalActionKnown) {
        fetchCriticalAction();
        return;
    }

    notify(level);
}

void BatteryWarning::notify(uint level)
{
    if (level == Low)
        m_notification->setSummary(tr("Low Battery"));
    else if (level == Critical)
        m_notification->setSummary(tr("Battery Critically Low"));
    else
        m_notification->setSummary(tr("Battery Empty"));

    m_notification->setBody(body(level));
    m_notification->setUrgency(level == Low ? Cutefish::Notification::Normal
                                            : Cutefish::Notification::Critical);
    m_shownLevel = level;
    m_notification->show();
}

QString BatteryWarning::body(uint level) const
{
    if (level == Low) {
        const int percent = qRound(m_properties.value(QStringLiteral("Percentage")).toDouble());
        const qlonglong time = m_properties.value(QStringLiteral("TimeToEmpty")).toLongLong();

        // UPower reports 0 until it trusts the discharge rate.
        if (time >= 60)
            return tr("%1% of battery remaining (%2).").arg(percent).arg(Battery::formatDuration(time));

        return tr("%1% of battery remaining.").arg(percent);
    }

    const bool powerOff = m_criticalAction == QLatin1String("PowerOff");
    const bool hibernate = m_criticalAction == QLatin1String("Hibernate")
            || m_criticalAction == QLatin1String("HybridSleep");
    const bool suspend = m_criticalAction == QLatin1String("Suspend");

    if (level == Critical) {
        if (powerOff)
            return tr("Save your work and connect to power. The computer will shut down when the battery is nearly empty.");
        if (hibernate)
            return tr("Connect to power. The computer will hibernate when the battery is nearly empty.");
        if (suspend)
            return tr("Save your work and connect to power. The computer will go to sleep when the battery is nearly empty.");

        return tr("Connect to power soon.");
    }

    // UPower waits 20 seconds before the action and drops it once power returns.
    if (powerOff)
        return tr("The computer is about to shut down. Connect to power to keep it running.");
    if (hibernate)
        return tr("The computer is about to hibernate. Connect to power to keep it running.");
    if (suspend)
        return tr("The computer is about to go to sleep. Connect to power to keep it running.");

    return tr("Connect to power now.");
}
