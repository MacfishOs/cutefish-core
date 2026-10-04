// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BATTERYWARNING_H
#define BATTERYWARNING_H

#include <QObject>
#include <QVariantMap>

// Warns at the levels UPower.conf sets, before UPower's critical power action.
class BatteryWarning : public QObject
{
    Q_OBJECT

public:
    // UpDeviceLevel; UPower keeps it at None unless the battery is discharging.
    enum Level { Unknown, None, Discharging, Low, Critical, Action };

    explicit BatteryWarning(QObject *parent = nullptr);

private slots:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                             const QStringList &invalidated);
    void onNotificationClosed(uint id, uint reason);

private:
    void fetchProperties();
    void fetchCriticalAction();
    void update();
    void notify(uint level);
    QString body(uint level) const;

    QVariantMap m_properties;
    QString m_criticalAction;
    bool m_criticalActionKnown;
    // A Notify or GetCriticalAction call is in flight; its reply runs update() again.
    bool m_pending;
    uint m_shownLevel;
    uint m_notificationId;
};

#endif // BATTERYWARNING_H
