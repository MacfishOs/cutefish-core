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

#include "dimdisplayaction.h"

#include <utility>

#include <QDebug>
#include <QTimer>

namespace {
constexpr auto s_screenSaverService = "org.freedesktop.ScreenSaver";
constexpr auto s_screenSaverPath = "/ScreenSaver";
constexpr auto s_screenSaverInterface = "org.freedesktop.ScreenSaver";
constexpr auto s_logindService = "org.freedesktop.login1";
constexpr auto s_logindPath = "/org/freedesktop/login1";
constexpr auto s_logindInterface = "org.freedesktop.login1.Manager";

constexpr int s_brightnessDimTimeoutNumerator = 1;
constexpr int s_brightnessDimTimeoutDenominator = 2;
constexpr int s_brightnessDimTimeoutFinalNumerator = 3;
constexpr int s_brightnessDimTimeoutFinalDenominator = 4;

// A locked screen nobody is using goes dark this soon, whatever the user chose.
constexpr int s_lockedDisplayOffTimeout = 30;

// Bounds the wait for a display change to settle, so a closed lid still sleeps.
constexpr int s_dockedLidRetryInterval = 500;
constexpr int s_dockedLidRetries = 10;
} // namespace

using cutefish::display::ipc::ErrorCode;

DimDisplayAction::DimDisplayAction(QObject *parent)
    : Action(parent)
    , m_iface("com.cutefish.Services", "/com/cutefish/Services/Brightness",
              "com.cutefish.Services.Brightness", QDBusConnection::sessionBus())
    , m_display(this)
{
    QDBusConnection::sessionBus().connect(
            QString::fromLatin1(s_screenSaverService), QString::fromLatin1(s_screenSaverPath),
            QString::fromLatin1(s_screenSaverInterface), QStringLiteral("ActiveChanged"), this,
            SLOT(onScreenSaverActiveChanged(bool)));
    QDBusConnection::systemBus().connect(
            QString::fromLatin1(s_logindService), QString::fromLatin1(s_logindPath),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
            this, SLOT(onLogindPropertiesChanged(QString, QVariantMap, QStringList)));
    m_suspendTimer.setSingleShot(true);
    connect(&m_suspendTimer, &QTimer::timeout, this, &DimDisplayAction::onSuspendTimerTimeout);
    m_dockedLidRetryTimer.setSingleShot(true);
    m_dockedLidRetryTimer.setInterval(s_dockedLidRetryInterval);
    connect(&m_dockedLidRetryTimer, &QTimer::timeout, this, [this] {
        requestDockedLid(m_lidClosed);
    });
    connect(&m_display, &DisplayClient::stateChanged, this,
            &DimDisplayAction::onDisplayStateChanged);
}

void DimDisplayAction::setLidPresent(bool present)
{
    if (m_lidPresent == present)
        return;

    m_lidPresent = present;
    if (m_lidPresent) {
        inhibitSystemdLidSwitch();
    } else {
        m_lidSwitchInhibitor = QDBusUnixFileDescriptor();
    }
}

bool DimDisplayAction::hasNativeDisplayPower() const
{
    return m_display.available();
}

bool DimDisplayAction::setDisplayPower(bool on)
{
    return m_display.setDisplayPower(on);
}

void DimDisplayAction::onWakeupFromIdle()
{
    m_lockedIdle = false;
    if (!m_dimmed) {
        return;
    }

    m_dimmed = false;
    m_suspendRequested = false;
    if (!m_lidClosed) {
        m_suspendTimer.stop();
        m_suspendDue = false;
        m_displayOffTimer.invalidate();
    }

    // Wait one event loop turn for the screen to wake up before restoring it.
    QTimer::singleShot(0, this, [this]() {
        if (!m_lidClosed)
            restoreDisplay();
    });
}

void DimDisplayAction::onIdleTimeout(int msec)
{
    int sec = msec / 1000;

    if (sec == s_lockedDisplayOffTimeout) {
        m_lockedIdle = true;
        if (m_locked)
            turnDisplayOffWhenIdle();
    }

    // Policy changes can invalidate an already queued notification.
    if (m_dimOnIdleTime < 0)
        return;

    if (sec == m_dimOnIdleTime) {
        turnDisplayOffWhenIdle();
        return;
    }

    if (!hasNativeDisplayPower()
        && sec
                == (m_dimOnIdleTime * s_brightnessDimTimeoutFinalNumerator
                    / s_brightnessDimTimeoutFinalDenominator)) {
        if (m_oldScreenBrightness > 0) {
            const int newBrightness = qRound(m_oldScreenBrightness / 8.0);
            m_iface.asyncCall("setValue", QVariant::fromValue(newBrightness));
        }
        m_dimmed = true;
    } else if (!hasNativeDisplayPower()
               && sec
                       == (m_dimOnIdleTime * s_brightnessDimTimeoutNumerator
                           / s_brightnessDimTimeoutDenominator)) {
        m_oldScreenBrightness = m_iface.property("brightness").toInt();

        if (m_oldScreenBrightness > 0) {
            const int newBrightness = qRound(m_oldScreenBrightness / 2.0);
            m_iface.asyncCall("setValue", QVariant::fromValue(newBrightness));
        }
        m_dimmed = true;
    }
}

void DimDisplayAction::turnDisplayOffWhenIdle()
{
    if (m_displayOff)
        return;

    turnDisplayOff();
    m_dimmed = true;
    startSuspendCountdown();
    trySuspend();
}

void DimDisplayAction::setTimeout(int timeout)
{
    setPolicy(timeout, m_suspendDelay);
}

void DimDisplayAction::setPolicy(int timeout, int suspendDelay)
{
    unregisterIdleTimeout();

    m_dimOnIdleTime = timeout;
    m_suspendDelay = qMax(0, suspendDelay);
    m_suspendTimer.stop();
    if (!m_lidClosed)
        m_suspendDue = false;

    // Registered even for "never": the lock screen still goes dark.
    registerIdleTimeout(s_lockedDisplayOffTimeout);

    if (timeout >= 0) {
        const int halfTimeout =
                timeout * s_brightnessDimTimeoutNumerator / s_brightnessDimTimeoutDenominator;
        const int finalDimTimeout = timeout * s_brightnessDimTimeoutFinalNumerator
                / s_brightnessDimTimeoutFinalDenominator;

        if (halfTimeout > 0)
            registerIdleTimeout(halfTimeout);
        if (finalDimTimeout > halfTimeout && finalDimTimeout < timeout)
            registerIdleTimeout(finalDimTimeout);
        registerIdleTimeout(timeout);
    }

    if (m_displayOff && !m_lidDisplayOff && m_displayOffTimer.isValid() && m_suspendDelay > 0) {
        const qint64 remaining = qint64(m_suspendDelay) * 1000 - m_displayOffTimer.elapsed();
        if (remaining <= 0) {
            m_suspendDue = true;
            trySuspend();
        } else {
            m_suspendTimer.start(int(remaining));
        }
    }
}

void DimDisplayAction::onScreenSaverActiveChanged(bool active)
{
    m_locked = active;
    if (active) {
        // Locked after the user had already left: nobody is there to see it.
        if (m_lockedIdle)
            turnDisplayOffWhenIdle();
        return;
    }
    m_dimmed = false;
    if (!m_lidClosed) {
        m_suspendTimer.stop();
        m_suspendDue = false;
        m_displayOffTimer.invalidate();
        if (m_displayOff)
            restoreDisplay();
    }
}

void DimDisplayAction::onLogindPropertiesChanged(const QString &interfaceName,
                                                 const QVariantMap &changedProperties,
                                                 const QStringList &invalidatedProperties)
{
    if (interfaceName != QString::fromLatin1(s_logindInterface))
        return;

    if (!changedProperties.contains(QStringLiteral("BlockInhibited"))
        && !changedProperties.contains(QStringLiteral("BlockWeakInhibited"))
        && !changedProperties.contains(QStringLiteral("DelayInhibited"))
        && !invalidatedProperties.contains(QStringLiteral("BlockInhibited"))
        && !invalidatedProperties.contains(QStringLiteral("BlockWeakInhibited"))
        && !invalidatedProperties.contains(QStringLiteral("DelayInhibited"))) {
        return;
    }

    trySuspend();
}

void DimDisplayAction::onSuspendTimerTimeout()
{
    if (!m_displayOff)
        return;

    m_suspendDue = true;
    trySuspend();
}

bool DimDisplayAction::lockSession()
{
    QDBusInterface screenSaver(
            QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"),
            QStringLiteral("org.freedesktop.ScreenSaver"), QDBusConnection::sessionBus());
    if (!screenSaver.isValid())
        return false;

    // Wait for the compositor to establish the lock before continuing.
    const QDBusMessage reply = screenSaver.call(QStringLiteral("Lock"));
    if (reply.type() != QDBusMessage::ReplyMessage) {
        qWarning() << "Unable to lock the session:" << reply.errorMessage();
        return false;
    }

    return true;
}

bool DimDisplayAction::suspendSession()
{
    QDBusInterface session(QStringLiteral("com.cutefish.Session"), QStringLiteral("/Session"),
                           QStringLiteral("com.cutefish.Session"), QDBusConnection::sessionBus());
    if (!session.isValid())
        return false;

    const QDBusMessage reply = session.call(QStringLiteral("suspend"));
    return reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()
            && reply.arguments().constFirst().toBool();
}

void DimDisplayAction::trySuspend()
{
    if (!m_suspendDue || !m_displayOff || m_suspendRequested)
        return;

    if (suspendSession()) {
        m_suspendRequested = true;
        m_suspendDue = false;
    }
}

void DimDisplayAction::startSuspendCountdown()
{
    m_suspendTimer.stop();
    m_suspendDue = false;

    if (m_lidClosed)
        return;

    m_displayOffTimer.restart();
    if (m_suspendDelay > 0)
        m_suspendTimer.start(m_suspendDelay * 1000);
}

void DimDisplayAction::turnDisplayOff()
{
    if (m_displayOff)
        return;

    m_displayPoweredOff = setDisplayPower(false);

    if (!m_displayPoweredOff) {
        if (m_oldScreenBrightness < 0)
            m_oldScreenBrightness = m_iface.property("brightness").toInt();

        if (m_oldScreenBrightness > 0)
            m_iface.asyncCall("setValue", QVariant::fromValue(0));
    }

    m_displayOff = true;
}

void DimDisplayAction::restoreDisplay()
{
    if (m_displayPoweredOff) {
        setDisplayPower(true);
        m_displayPoweredOff = false;
    }

    if (m_oldScreenBrightness >= 0)
        m_iface.asyncCall("setValue", QVariant::fromValue(m_oldScreenBrightness));

    m_oldScreenBrightness = -1;
    m_displayOff = false;
    m_displayOffTimer.invalidate();
}

void DimDisplayAction::handleLidClosed()
{
    m_lidClosed = true;

    if (!m_lidPresent)
        return;

    // The compositor decides whether an external display stays in use.
    m_dockedLidRetries = 0;
    requestDockedLid(true);
}

void DimDisplayAction::sleepWithLidClosed(bool waitForLock)
{
    if (!m_displayOff) {
        // Suspend even without the lock: the compositor locks as the displays go
        // off and again before sleep, and logind's own lid handling is inhibited.
        if (waitForLock && !lockSession())
            qWarning() << "Suspending with the lid closed before the session is locked";

        turnDisplayOff();
        m_dimmed = true;
    }

    m_lidDisplayOff = true;
    m_suspendTimer.stop();
    m_displayOffTimer.invalidate();
    if (!m_suspendRequested) {
        m_suspendDue = true;
        trySuspend();
    }
}

void DimDisplayAction::handleLidOpened()
{
    m_lidClosed = false;
    m_suspendRequested = false;
    m_suspendDue = false;
    m_suspendTimer.stop();
    m_displayOffTimer.invalidate();
    m_dockedLidRecheck = false;
    m_dockedLidRetries = 0;
    m_dockedLidRetryTimer.stop();

    if (m_dockedLidPending || m_dockedLidActive) {
        m_dockedLidPending = false;
        m_dockedLidActive = false;
        requestDockedLid(false);
        m_lidDisplayOff = false;
        return;
    }

    if (!m_lidDisplayOff)
        return;

    m_lidDisplayOff = false;
    restoreDisplay();
}

void DimDisplayAction::requestDockedLid(bool closed)
{
    m_dockedLidRetryTimer.stop();
    const quint64 serial = ++m_dockedLidSerial;
    const bool sent = m_display.setDockedLidClosed(
            closed, [this, serial, closed](bool succeeded, ErrorCode error) {
                // A later lid event has superseded this request.
                if (serial == m_dockedLidSerial)
                    finishDockedLid(closed, succeeded, error);
            });

    m_dockedLidPending = sent && closed;
    if (!sent && closed)
        finishDockedLid(true, false, ErrorCode::Unavailable);
}

void DimDisplayAction::finishDockedLid(bool closed, bool succeeded, ErrorCode error)
{
    const bool recheck = std::exchange(m_dockedLidRecheck, false);
    m_dockedLidPending = false;

    // A display change in progress answers Busy; ask again once it settles.
    if (!succeeded && error == ErrorCode::Busy && m_dockedLidRetries < s_dockedLidRetries) {
        ++m_dockedLidRetries;
        m_dockedLidRetryTimer.start();
        return;
    }

    m_dockedLidRetries = 0;
    if (!closed)
        return;

    if (succeeded) {
        m_dockedLidActive = true;
        m_lidDisplayOff = true;
        if (recheck)
            requestDockedLid(true);
        return;
    }

    // After an undock the internal panel is already off, so a lock would wait for a
    // frame it never presents; powering every display off locks the session instead.
    const bool wasDocked = std::exchange(m_dockedLidActive, false);
    sleepWithLidClosed(!wasDocked);
}

void DimDisplayAction::onDisplayStateChanged()
{
    const auto &state = m_display.state();

    // Nothing is applied while a display change is still in progress.
    if (state.appliedRevision == 0)
        return;

    auto topology =
            std::make_tuple(state.instanceId, state.topologyGeneration, state.appliedRevision);
    if (topology == m_displayTopology)
        return;
    m_displayTopology = std::move(topology);

    // logind's lid handling is inhibited, so losing the external display while
    // docked would otherwise leave a closed laptop awake.
    if (!m_lidClosed)
        return;

    if (m_dockedLidPending)
        m_dockedLidRecheck = true;
    else if (m_dockedLidActive)
        requestDockedLid(true);
}

void DimDisplayAction::inhibitSystemdLidSwitch()
{
    if (!m_lidPresent || m_lidSwitchInhibitor.isValid()) {
        return;
    }

    QDBusInterface logind(QString::fromLatin1(s_logindService), QString::fromLatin1(s_logindPath),
                          QString::fromLatin1(s_logindInterface), QDBusConnection::systemBus());
    if (!logind.isValid())
        return;

    const QDBusMessage reply = logind.call(
            QStringLiteral("Inhibit"), QStringLiteral("handle-lid-switch"),
            QStringLiteral("Cutefish Settings"),
            QStringLiteral("Cutefish manages lid power policy"), QStringLiteral("block"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        qWarning() << "Unable to inhibit systemd lid handling:" << reply.errorMessage();
        return;
    }

    const auto inhibitor = reply.arguments().constFirst().value<QDBusUnixFileDescriptor>();
    if (!inhibitor.isValid()) {
        qWarning() << "systemd returned an invalid lid inhibitor";
        return;
    }

    m_lidSwitchInhibitor = inhibitor;
}
