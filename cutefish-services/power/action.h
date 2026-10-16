// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2010 Dario Freddi <drf@kde.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CUTEFISH_SETTINGS_DAEMON_ACTION_H
#define CUTEFISH_SETTINGS_DAEMON_ACTION_H

class IdleManager;

class Action
{
public:
    /**
     * Default constructor
     */
    explicit Action(IdleManager *idle);
    /**
     * Default destructor
     */
    virtual ~Action();

    Action(const Action &) = delete;
    Action &operator=(const Action &) = delete;

    /**
     * This function is meant to find out if this action is available on this system. Actions
     * CAN reimplement this function if they are dependent on specific hardware/software
     * requirements. By default, this function will always return true.
     *
     * Should this function return false, the core will delete and ignore the action right after
     * creation.
     *
     * @returns Whether this action is supported or not by the current system
     */
    virtual bool isSupported();

    /**
     * This is called whenever an idle timeout registered with registerIdleTimeout is
     * reached.
     *
     * @param msec The idle timeout reached in milliseconds
     */
    virtual void onIdleTimeout(int msec) = 0;
    /**
     * This is called whenever the PC wakes up from an Idle state. It is @b always called
     * after a registered idle timeout has been reached.
     */
    virtual void onWakeupFromIdle() = 0;

protected:
    // Registration uses seconds; onIdleTimeout receives milliseconds. Without
    // an idle manager, as in tests, nothing is registered and -1 returned.
    int registerIdleTimeout(int seconds);

    void unregisterIdleTimeout(int identifier);

private:
    IdleManager *m_idle;
};

#endif // CUTEFISH_SETTINGS_DAEMON_ACTION_H
