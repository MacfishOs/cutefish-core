// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "batterywarning.h"
#include "battery.h"
#include "power.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace {
const QString displayDevice = QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice");
const QString notificationsService = QStringLiteral("org.freedesktop.Notifications");
const QString notificationsPath = QStringLiteral("/org/freedesktop/Notifications");
}

BatteryWarning::BatteryWarning(QObject *parent)
    : QObject(parent)
    , m_criticalActionKnown(false)
    , m_pending(false)
    , m_shownLevel(None)
    , m_notificationId(0)
{
    QDBusConnection::systemBus().connect(UP_DBUS_SERVICE, displayDevice,
                                         QStringLiteral("org.freedesktop.DBus.Properties"),
                                         QStringLiteral("PropertiesChanged"), this,
                                         SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    QDBusConnection::sessionBus().connect(notificationsService, notificationsPath,
                                          notificationsService, QStringLiteral("NotificationClosed"),
                                          this, SLOT(onNotificationClosed(uint, uint)));

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

void BatteryWarning::onNotificationClosed(uint id, uint reason)
{
    Q_UNUSED(reason);

    // Dismissed or expired: a later level gets a banner of its own.
    if (id == m_notificationId)
        m_notificationId = 0;
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

    m_pending = true;

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();

        QDBusPendingReply<QString> reply = *watcher;
        m_criticalAction = reply.isValid() ? reply.value() : QString();
        m_criticalActionKnown = true;
        m_pending = false;
        update();
    });
}

void BatteryWarning::update()
{
    if (m_pending)
        return;

    const uint level = m_properties.value(QStringLiteral("WarningLevel")).toUInt();

    if (level < Low) {
        if (m_notificationId) {
            QDBusMessage message = QDBusMessage::createMethodCall(notificationsService,
                    notificationsPath, notificationsService, QStringLiteral("CloseNotification"));
            message.setAutoStartService(false);
            message << m_notificationId;
            QDBusConnection::sessionBus().asyncCall(message);
        }

        m_notificationId = 0;
        m_shownLevel = None;
        // logind may allow hibernation by the next discharge.
        m_criticalActionKnown = false;
        return;
    }

    // Each level is announced once per discharge; a dismissed warning stays dismissed.
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
    QString summary;
    if (level == Low)
        summary = tr("Low Battery");
    else if (level == Critical)
        summary = tr("Battery Critically Low");
    else
        summary = tr("Battery Empty");

    QVariantMap hints;
    // Critical urgency keeps the banner up until it is dismissed.
    hints.insert(QStringLiteral("urgency"), QVariant::fromValue<uchar>(level == Low ? 1 : 2));
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("cutefish-settings"));

    QDBusMessage message = QDBusMessage::createMethodCall(notificationsService, notificationsPath,
            notificationsService, QStringLiteral("Notify"));
    // Only a running notification server shows it; none is started for it.
    message.setAutoStartService(false);
    // No app name: the shell titles it with the desktop entry's localized name.
    message << QString() << m_notificationId
            << QStringLiteral("battery-caution") << summary << body(level) << QStringList()
            << hints << -1;

    const uint shownLevel = m_shownLevel;
    m_pending = true;
    m_shownLevel = level;

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, shownLevel](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        m_pending = false;

        QDBusPendingReply<uint> reply = *watcher;
        // No notification server yet, as early in a session: the next battery change retries.
        if (reply.isError()) {
            m_shownLevel = shownLevel;
            return;
        }

        m_notificationId = reply.value();
        update();
    });
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
