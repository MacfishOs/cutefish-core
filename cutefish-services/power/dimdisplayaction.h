/***************************************************************************
 *   Copyright (C) 2021 by Reion Wong <reion@cutefishos.com>               *
 *   Copyright (C) 2010 by Dario Freddi <drf@kde.org>                      *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA .        *
 ***************************************************************************/

#ifndef DIMDISPLAYACTION_H
#define DIMDISPLAYACTION_H

#include <tuple>

#include <QDBusInterface>
#include <QDBusUnixFileDescriptor>
#include <QElapsedTimer>
#include <QTimer>

#include "action.h"
#include "displayclient.h"

class DimDisplayAction : public Action
{
    Q_OBJECT

public:
    explicit DimDisplayAction(QObject *parent = nullptr);

    void onWakeupFromIdle() override;
    void onIdleTimeout(int msec) override;
    void setTimeout(int timeout) override;
    void setPolicy(int timeout, int suspendDelay);
    void setLidPresent(bool present);
    void handleLidClosed();
    void handleLidOpened();

private Q_SLOTS:
    void onScreenSaverActiveChanged(bool active);
    void onLogindPropertiesChanged(const QString &interfaceName,
                                   const QVariantMap &changedProperties,
                                   const QStringList &invalidatedProperties);
    void onSuspendTimerTimeout();
    void onDisplayStateChanged();

private:
    bool lockSession();
    bool suspendSession();
    void trySuspend();
    void turnDisplayOff();
    void restoreDisplay();
    void startSuspendCountdown();
    void turnDisplayOffWhenIdle();
    void sleepWithLidClosed(bool waitForLock);
    void inhibitSystemdLidSwitch();
    bool hasNativeDisplayPower() const;
    bool setDisplayPower(bool on);
    void requestDockedLid(bool closed);
    void finishDockedLid(bool closed, bool succeeded, cutefish::display::ipc::ErrorCode error);

    QDBusInterface m_iface;
    DisplayClient m_display;
    int m_dimOnIdleTime = 0;
    int m_suspendDelay = 900;
    int m_oldScreenBrightness = -1;
    bool m_dimmed = false;
    bool m_displayPoweredOff = false;
    bool m_displayOff = false;
    bool m_suspendRequested = false;
    bool m_suspendDue = false;
    bool m_lidClosed = false;
    bool m_lidPresent = false;
    bool m_lidDisplayOff = false;
    bool m_locked = false;
    // The user has been away for the locked timeout since their last input.
    bool m_lockedIdle = false;
    bool m_dockedLidPending = false;
    bool m_dockedLidActive = false;
    bool m_dockedLidRecheck = false;
    int m_dockedLidRetries = 0;
    quint64 m_dockedLidSerial = 0;
    QTimer m_dockedLidRetryTimer;
    std::tuple<QString, qulonglong, qulonglong> m_displayTopology;

    QDBusUnixFileDescriptor m_lidSwitchInhibitor;

    QTimer m_suspendTimer;
    QElapsedTimer m_displayOffTimer;
};

#endif // DIMDISPLAYACTION_H
