// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2010 Dario Freddi <drf@kde.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "idlemanager.h"

#include <algorithm>
#include <climits>

IdleManager::IdleManager()
{
    m_subscriptions.push_back(m_idle.timeoutReached.connect([this](int identifier, int timeout) {
        onIdleTimeoutReached(identifier, timeout);
    }));
    m_subscriptions.push_back(m_idle.resumed.connect([this] {
        onResumingFromIdle();
    }));
}

IdleManager::~IdleManager() = default;

int IdleManager::registerActionTimeout(Action *action, int secs)
{
    if (secs < 0 || secs > INT_MAX / 1000)
        return -1;
    int identifier = m_idle.addTimeout(secs * 1000);

    m_registeredActionTimeouts[action].push_back(identifier);

    return identifier;
}

void IdleManager::unregisterActionTimeout(Action *action, int identifier)
{
    auto it = m_registeredActionTimeouts.find(action);
    if (it == m_registeredActionTimeouts.end())
        return;
    auto entry = std::find(it->second.begin(), it->second.end(), identifier);
    if (entry == it->second.end())
        return;
    it->second.erase(entry);

    m_idle.removeTimeout(identifier);
}

void IdleManager::unregisterActionTimeouts(Action *action)
{
    const auto it = m_registeredActionTimeouts.find(action);
    if (it != m_registeredActionTimeouts.end()) {
        for (int id : it->second)
            m_idle.removeTimeout(id);
        m_registeredActionTimeouts.erase(it);
    }

    m_pendingResumeFromIdleActions.erase(action);
}

void IdleManager::onIdleTimeoutReached(int identifier, int timeout)
{
    for (const auto &[action, identifiers] : m_registeredActionTimeouts) {
        if (std::find(identifiers.begin(), identifiers.end(), identifier) != identifiers.end()) {
            m_pendingResumeFromIdleActions.insert(action);
            action->onIdleTimeout(timeout);

            break;
        }
    }
}

void IdleManager::onResumingFromIdle()
{
    // An action may go while another one handles the wake; it leaves the set then.
    std::set<Action *> pending = std::move(m_pendingResumeFromIdleActions);
    m_pendingResumeFromIdleActions.clear();
    for (Action *action : pending) {
        if (m_registeredActionTimeouts.contains(action))
            action->onWakeupFromIdle();
    }
}
