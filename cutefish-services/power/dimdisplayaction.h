// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2010 Dario Freddi <drf@kde.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DIMDISPLAYACTION_H
#define DIMDISPLAYACTION_H

#include <tuple>

#include <QDBusUnixFileDescriptor>
#include <QElapsedTimer>
#include <QTimer>

#include "action.h"
#include "displayclient.h"

class BrightnessManager;

class DimDisplayAction : public Action
{
    Q_OBJECT

public:
    explicit DimDisplayAction(BrightnessManager *brightness, QObject *parent = nullptr);

    void onWakeupFromIdle() override;
    void onIdleTimeout(int msec) override;
    void setPolicy(int timeout, int suspendDelay);
    void setLidPresent(bool present);
    void handleLidClosed();
    void handleLidOpened();

private Q_SLOTS:
    void onPrepareForSleep(bool sleeping);
    void onScreenSaverActiveChanged(bool active);
    void onLogindPropertiesChanged(const QString &interfaceName,
                                   const QVariantMap &changedProperties,
                                   const QStringList &invalidatedProperties);
    void onSuspendTimerTimeout();
    void onDisplayStateChanged();

private:
    bool lockSession();
    void trySuspend();
    void turnDisplayOff();
    void restoreDisplay();
    void startSuspendCountdown();
    void stopSuspendCountdown();
    void turnDisplayOffWhenIdle();
    void sleepWithLidClosed(bool waitForLock);
    bool idleSleepApplies() const;
    void updateLidSwitchInhibitor();
    bool hasNativeDisplayPower() const;
    bool setDisplayPower(bool on);
    void requestDockedLid(bool closed);
    void finishDockedLid(bool closed, bool succeeded, cutefish::display::ipc::ErrorCode error);

    BrightnessManager *m_brightness;
    DisplayClient m_display;
    int m_dimOnIdleTime = 0;
    int m_suspendDelay = 900;
    int m_oldScreenBrightness = -1;
    bool m_dimmed = false;
    bool m_displayPoweredOff = false;
    bool m_displayOff = false;
    bool m_suspendRequested = false;
    bool m_suspendDue = false;
    quint64 m_suspendSerial = 0;
    bool m_lidClosed = false;
    bool m_lidPresent = false;
    bool m_lidDisplayOff = false;
    // Closed with no display in use: logind handles the lid until it opens.
    bool m_lidSleeping = false;
    bool m_locked = false;
    // The user has been away for the locked timeout since their last input.
    bool m_lockedIdle = false;
    int m_lockedIdleTimeout = -1;
    QList<int> m_policyIdleTimeouts;
    bool m_dockedLidPending = false;
    bool m_dockedLidActive = false;
    bool m_dockedLidRecheck = false;
    int m_dockedLidRetries = 0;
    quint64 m_dockedLidSerial = 0;
    QTimer m_dockedLidRetryTimer;
    std::tuple<QString, qulonglong, qulonglong> m_displayTopology;

    QDBusUnixFileDescriptor m_lidSwitchInhibitor;
    bool m_lidSwitchInhibiting = false;

    QTimer m_suspendTimer;
    QElapsedTimer m_displayOffTimer;
};

#endif // DIMDISPLAYACTION_H
