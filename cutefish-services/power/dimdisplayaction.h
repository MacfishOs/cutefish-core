// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2010 Dario Freddi <drf@kde.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DIMDISPLAYACTION_H
#define DIMDISPLAYACTION_H

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "action.h"
#include "base/dbus.h"
#include "base/elapsed_timer.h"
#include "base/signal.h"
#include "base/timer.h"
#include "cutefish/display/display_client.h"

class BrightnessManager;

class DimDisplayAction : public Action
{
public:
    // The screen saver on `session`, logind on `system`; `displayService`
    // names the compositor's display service, which tests run themselves.
    DimDisplayAction(IdleManager *idle, BrightnessManager *brightness,
                     cutefish::base::dbus::Connection *session, cutefish::base::dbus::Connection *system,
                     const std::string &displayService = "com.cutefish.DisplayConfig");
    ~DimDisplayAction() override;

    void onWakeupFromIdle() override;
    void onIdleTimeout(int msec) override;
    void setPolicy(int timeout, int suspendDelay);
    void setLidPresent(bool present);
    void handleLidClosed();
    void handleLidOpened();

private:
    void onPrepareForSleep(bool sleeping);
    void onScreenSaverActiveChanged(bool active);
    void onLogindPropertiesChanged(const cutefish::base::dbus::VariantMap &changedProperties,
                                   const std::vector<std::string> &invalidatedProperties);
    void onSuspendTimerTimeout();
    void onDisplayStateChanged();

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
    void finishDockedLid(bool closed, bool succeeded, cutefish::display::ErrorCode error);
    int screenBrightness() const;
    void setScreenBrightness(int value);

    BrightnessManager *m_brightness;
    cutefish::base::dbus::Proxy m_screenSaver;
    cutefish::base::dbus::Proxy m_logind;
    cutefish::display::DisplayClient m_display;
    std::vector<cutefish::base::dbus::Match> m_matches;
    cutefish::base::Subscription m_displayStateChanged;
    cutefish::base::dbus::PendingCall m_suspendCall;
    cutefish::base::dbus::PendingCall m_inhibitCall;
    int m_dimOnIdleTime = 0;
    int m_suspendDelay = 900;
    int m_oldScreenBrightness = -1;
    bool m_dimmed = false;
    bool m_displayPoweredOff = false;
    bool m_displayOff = false;
    bool m_suspendRequested = false;
    bool m_suspendDue = false;
    uint64_t m_suspendSerial = 0;
    bool m_lidClosed = false;
    bool m_lidPresent = false;
    bool m_lidDisplayOff = false;
    // Closed with no display in use: logind handles the lid until it opens.
    bool m_lidSleeping = false;
    bool m_locked = false;
    // The user has been away for the locked timeout since their last input.
    bool m_lockedIdle = false;
    int m_lockedIdleTimeout = -1;
    std::vector<int> m_policyIdleTimeouts;
    bool m_dockedLidPending = false;
    bool m_dockedLidActive = false;
    bool m_dockedLidRecheck = false;
    int m_dockedLidRetries = 0;
    uint64_t m_dockedLidSerial = 0;
    cutefish::base::Timer m_dockedLidRetryTimer;
    std::tuple<std::string, uint64_t, uint64_t> m_displayTopology;

    cutefish::base::dbus::UnixFd m_lidSwitchInhibitor;
    bool m_lidSwitchInhibiting = false;

    cutefish::base::Timer m_suspendTimer;
    cutefish::base::Timer m_wakeTimer;
    cutefish::base::ElapsedTimer m_displayOffTimer;
};

#endif // DIMDISPLAYACTION_H
