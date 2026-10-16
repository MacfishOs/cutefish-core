// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2010 Dario Freddi <drf@kde.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dimdisplayaction.h"
#include "brightness/brightnessmanager.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "log.h"

using namespace std::chrono_literals;
using namespace cutefish::base;

static constexpr auto s_screenSaverService = "org.freedesktop.ScreenSaver";
static constexpr auto s_screenSaverPath = "/ScreenSaver";
static constexpr auto s_screenSaverInterface = "org.freedesktop.ScreenSaver";
static constexpr auto s_logindService = "org.freedesktop.login1";
static constexpr auto s_logindPath = "/org/freedesktop/login1";
static constexpr auto s_logindInterface = "org.freedesktop.login1.Manager";

// Without native display power the screen dims at these steps before it goes off.
static int halfDimTimeout(int timeout)
{
    return timeout / 2;
}

static int finalDimTimeout(int timeout)
{
    return timeout * 3 / 4;
}

// A locked screen nobody is using goes dark this soon, whatever the user chose.
static constexpr int s_lockedDisplayOffTimeout = 30;

// Bounds the wait for a display change to settle, so a closed lid still sleeps.
static constexpr std::chrono::milliseconds s_dockedLidRetryInterval = 500ms;
static constexpr int s_dockedLidRetries = 10;

using cutefish::display::DisplayClient;
using cutefish::display::ErrorCode;

DimDisplayAction::DimDisplayAction(IdleManager *idle, BrightnessManager *brightness,
                                   dbus::Connection *session, dbus::Connection *system,
                                   const std::string &displayService)
    : Action(idle)
    , m_brightness(brightness)
    , m_screenSaver(session, s_screenSaverService, s_screenSaverPath, s_screenSaverInterface)
    , m_logind(system, s_logindService, s_logindPath, s_logindInterface)
    , m_display(displayService)
{
    if (session) {
        m_matches.push_back(m_screenSaver.onSignal("ActiveChanged", [this](const dbus::Message &message) {
            if (const auto active = message.argument<bool>())
                onScreenSaverActiveChanged(*active);
        }));
    }
    if (system) {
        m_matches.push_back(m_logind.onPropertiesChanged(
                [this](const dbus::VariantMap &changed, const std::vector<std::string> &invalidated) {
                    onLogindPropertiesChanged(changed, invalidated);
                }));
        m_matches.push_back(m_logind.onSignal("PrepareForSleep", [this](const dbus::Message &message) {
            if (const auto sleeping = message.argument<bool>())
                onPrepareForSleep(*sleeping);
        }));
    }
    m_suspendTimer.setSingleShot(true);
    m_suspendTimer.onTimeout = [this] {
        onSuspendTimerTimeout();
    };
    m_dockedLidRetryTimer.setSingleShot(true);
    m_dockedLidRetryTimer.setInterval(s_dockedLidRetryInterval);
    m_dockedLidRetryTimer.onTimeout = [this] {
        requestDockedLid(m_lidClosed);
    };
    m_wakeTimer.setSingleShot(true);
    m_wakeTimer.setInterval(0ms);
    m_wakeTimer.onTimeout = [this] {
        if (!m_lidClosed) {
            restoreDisplay();
        } else if (m_dockedLidActive && m_displayPoweredOff) {
            // Input has already woken the external displays; powering every display
            // on would also light the closed panel.
            m_displayPoweredOff = false;
            m_displayOff = false;
            m_displayOffTimer.invalidate();
        }
    };
    m_displayStateChanged = m_display.stateChanged.connect([this] {
        onDisplayStateChanged();
    });
}

DimDisplayAction::~DimDisplayAction() = default;

void DimDisplayAction::setLidPresent(bool present)
{
    if (m_lidPresent == present)
        return;

    m_lidPresent = present;
    updateLidSwitchInhibitor();
}

bool DimDisplayAction::hasNativeDisplayPower() const
{
    return m_display.available();
}

bool DimDisplayAction::setDisplayPower(bool on)
{
    return m_display.setDisplayPower(on);
}

int DimDisplayAction::screenBrightness() const
{
    return m_brightness ? m_brightness->brightness() : -1;
}

void DimDisplayAction::setScreenBrightness(int value)
{
    if (m_brightness)
        m_brightness->setValue(value);
}

void DimDisplayAction::onWakeupFromIdle()
{
    m_lockedIdle = false;
    if (!m_dimmed) {
        return;
    }

    m_dimmed = false;
    m_suspendRequested = false;
    if (idleSleepApplies())
        stopSuspendCountdown();

    // Wait one event loop turn for the screen to wake up before restoring it.
    m_wakeTimer.start();
}

void DimDisplayAction::onPrepareForSleep(bool sleeping)
{
    if (sleeping) {
        // Whoever asked for it, this sleep is the one that was due.
        m_suspendTimer.stop();
        m_suspendDue = false;
        return;
    }

    m_suspendRequested = false;

    // A wake without input (a timer, the power supply, a swallowed key) leaves the
    // display off; sleep again unless someone comes back. logind does this for a
    // closed lid once its holdoff has passed.
    if (m_displayOff && idleSleepApplies())
        startSuspendCountdown();
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

    if (hasNativeDisplayPower())
        return;

    if (sec == finalDimTimeout(m_dimOnIdleTime)) {
        if (m_oldScreenBrightness > 0) {
            const int newBrightness = int(std::lround(m_oldScreenBrightness / 8.0));
            setScreenBrightness(newBrightness);
        }
        m_dimmed = true;
    } else if (sec == halfDimTimeout(m_dimOnIdleTime)) {
        m_oldScreenBrightness = screenBrightness();

        if (m_oldScreenBrightness > 0) {
            const int newBrightness = int(std::lround(m_oldScreenBrightness / 2.0));
            setScreenBrightness(newBrightness);
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

void DimDisplayAction::setPolicy(int timeout, int suspendDelay)
{
    for (int identifier : std::exchange(m_policyIdleTimeouts, {}))
        unregisterIdleTimeout(identifier);

    m_dimOnIdleTime = timeout;
    m_suspendDelay = std::max(0, suspendDelay);
    m_suspendTimer.stop();
    if (idleSleepApplies())
        m_suspendDue = false;

    // Registered even for "never": the lock screen still goes dark.
    // Recreating restarts its countdown, but a fired one must stay to report the wake.
    if (m_lockedIdleTimeout >= 0 && !m_lockedIdle) {
        unregisterIdleTimeout(m_lockedIdleTimeout);
        m_lockedIdleTimeout = -1;
    }
    if (m_lockedIdleTimeout < 0)
        m_lockedIdleTimeout = registerIdleTimeout(s_lockedDisplayOffTimeout);

    if (timeout >= 0) {
        const int halfTimeout = halfDimTimeout(timeout);
        const int finalTimeout = finalDimTimeout(timeout);

        if (halfTimeout > 0)
            m_policyIdleTimeouts.push_back(registerIdleTimeout(halfTimeout));
        if (finalTimeout > halfTimeout && finalTimeout < timeout)
            m_policyIdleTimeouts.push_back(registerIdleTimeout(finalTimeout));
        m_policyIdleTimeouts.push_back(registerIdleTimeout(timeout));
    }

    if (m_displayOff && idleSleepApplies() && m_displayOffTimer.isValid() && m_suspendDelay > 0) {
        const int64_t remaining = int64_t(m_suspendDelay) * 1000 - m_displayOffTimer.elapsedMs();
        if (remaining <= 0) {
            m_suspendDue = true;
            trySuspend();
        } else {
            m_suspendTimer.start(std::chrono::milliseconds(remaining));
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
    if (idleSleepApplies())
        stopSuspendCountdown();
    if (!m_lidClosed && m_displayOff)
        restoreDisplay();
}

void DimDisplayAction::onLogindPropertiesChanged(const dbus::VariantMap &changedProperties,
                                                 const std::vector<std::string> &invalidatedProperties)
{
    static const char *const names[] = {"BlockInhibited", "BlockWeakInhibited", "DelayInhibited"};
    const bool inhibitorsChanged = std::any_of(std::begin(names), std::end(names), [&](const char *name) {
        return changedProperties.contains(name)
                || std::find(invalidatedProperties.begin(), invalidatedProperties.end(), name)
                != invalidatedProperties.end();
    });
    if (!inhibitorsChanged)
        return;

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
    if (!m_screenSaver.isValid())
        return false;

    // Wait for the compositor to establish the lock before continuing.
    const dbus::Reply reply = m_screenSaver.call("Lock");
    if (reply.isError()) {
        CF_WARNING(services::log()) << "Unable to lock the session:" << reply.error().message;
        return false;
    }

    return true;
}

void DimDisplayAction::trySuspend()
{
    if (!m_suspendDue || !m_displayOff || m_suspendRequested)
        return;

    // Not interactive: with a sleep inhibitor held, logind would otherwise wait on a
    // password prompt nobody can see. A refusal is retried when the inhibitors change.
    m_suspendRequested = true;
    const uint64_t serial = ++m_suspendSerial;
    m_suspendCall = m_logind.callAsync(
            "Suspend",
            [this, serial](const dbus::Reply &reply) {
                if (serial != m_suspendSerial)
                    return;

                if (reply.isError()) {
                    CF_INFO(services::log()) << "Suspend refused:" << reply.error().message;
                    m_suspendRequested = false;
                    return;
                }

                m_suspendDue = false;
            },
            false);
}

void DimDisplayAction::startSuspendCountdown()
{
    m_suspendTimer.stop();
    m_suspendDue = false;

    if (!idleSleepApplies())
        return;

    m_displayOffTimer.restart();
    if (m_suspendDelay > 0)
        m_suspendTimer.start(std::chrono::seconds(m_suspendDelay));
}

void DimDisplayAction::stopSuspendCountdown()
{
    m_suspendTimer.stop();
    m_suspendDue = false;
    m_displayOffTimer.invalidate();
}

void DimDisplayAction::turnDisplayOff()
{
    if (m_displayOff)
        return;

    m_displayPoweredOff = setDisplayPower(false);

    if (!m_displayPoweredOff) {
        if (m_oldScreenBrightness < 0)
            m_oldScreenBrightness = screenBrightness();

        if (m_oldScreenBrightness > 0)
            setScreenBrightness(0);
    }

    m_displayOff = true;
}

void DimDisplayAction::restoreDisplay()
{
    if (m_displayPoweredOff) {
        setDisplayPower(true);
        m_displayPoweredOff = false;
    }

    if (m_oldScreenBrightness > 0)
        setScreenBrightness(m_oldScreenBrightness);

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
        // off and again before sleep.
        if (waitForLock && !lockSession())
            CF_WARNING(services::log()) << "Suspending with the lid closed before the session is locked";

        turnDisplayOff();
        m_dimmed = true;
    }

    m_lidDisplayOff = true;
    m_lidSleeping = true;
    m_suspendTimer.stop();
    m_displayOffTimer.invalidate();

    // logind's own lid handling sleeps through block inhibitors, and sleeps again
    // after a wake with the lid still closed.
    updateLidSwitchInhibitor();

    if (!m_suspendRequested) {
        m_suspendDue = true;
        trySuspend();
    }
}

bool DimDisplayAction::idleSleepApplies() const
{
    return !m_lidClosed || m_dockedLidActive;
}

void DimDisplayAction::handleLidOpened()
{
    m_lidClosed = false;
    m_lidSleeping = false;
    m_suspendRequested = false;
    stopSuspendCountdown();
    updateLidSwitchInhibitor();
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
    const uint64_t serial = ++m_dockedLidSerial;
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
    const DisplayClient::State &state = m_display.state();

    // Nothing is applied while a display change is still in progress.
    if (state.appliedRevision == 0)
        return;

    auto topology = std::make_tuple(state.instanceId, state.topologyGeneration, state.appliedRevision);
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

void DimDisplayAction::updateLidSwitchInhibitor()
{
    if (!m_lidPresent || m_lidSleeping) {
        m_lidSwitchInhibitor = dbus::UnixFd();
        return;
    }

    if (m_lidSwitchInhibitor.isValid() || m_lidSwitchInhibiting || !m_logind.isValid())
        return;

    m_lidSwitchInhibiting = true;
    m_inhibitCall = m_logind.callAsync(
            "Inhibit",
            [this](const dbus::Reply &reply) {
                m_lidSwitchInhibiting = false;
                const auto inhibitor = reply.value<dbus::UnixFd>();
                if (!inhibitor) {
                    CF_WARNING(services::log())
                            << "Unable to inhibit systemd lid handling:" << reply.error().message;
                    return;
                }

                // A lid that closed meanwhile stays with logind.
                if (m_lidPresent && !m_lidSleeping)
                    m_lidSwitchInhibitor = *inhibitor;
            },
            std::string("handle-lid-switch"), std::string("Cutefish"),
            std::string("Cutefish manages lid power policy"), std::string("block"));
}
