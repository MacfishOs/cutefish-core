// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2010 Dario Freddi <drf@kde.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IDLEMANAGER_H
#define IDLEMANAGER_H

#include <map>
#include <set>
#include <vector>

#include "action.h"
#include "base/signal.h"
#include "idle/waylandidle.h"

class IdleManager
{
public:
    IdleManager();
    ~IdleManager();

    IdleManager(const IdleManager &) = delete;
    IdleManager &operator=(const IdleManager &) = delete;

    int registerActionTimeout(Action *action, int secs);
    void unregisterActionTimeout(Action *action, int identifier);
    // An action that goes takes its timeouts with it.
    void unregisterActionTimeouts(Action *action);

private:
    void onIdleTimeoutReached(int identifier, int timeout);
    void onResumingFromIdle();

    WaylandIdle m_idle;
    std::vector<cutefish::base::Subscription> m_subscriptions;
    std::map<Action *, std::vector<int>> m_registeredActionTimeouts;
    std::set<Action *> m_pendingResumeFromIdleActions;
};

#endif // IDLEMANAGER_H
