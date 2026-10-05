// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2010 Dario Freddi <drf@kde.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "idlemanager.h"

#include <QPointer>
#include <climits>
#include <utility>

IdleManager *IdleManager::self()
{
    static IdleManager mgr;
    return &mgr;
}

IdleManager::IdleManager(QObject *parent)
    : QObject(parent)
{
    connect(&m_idle, &WaylandIdle::timeoutReached, this, &IdleManager::onIdleTimeoutReached);
    connect(&m_idle, &WaylandIdle::resumed, this, &IdleManager::onResumingFromIdle);
}

int IdleManager::registerActionTimeout(Action *action, int secs)
{
    if (secs < 0 || secs > INT_MAX / 1000)
        return -1;
    if (!m_registeredActionTimeouts.contains(action))
        m_destroyConnections.insert(action,
                                    connect(action, &QObject::destroyed, this, [this, action] {
                                        unregisterActionTimeouts(action);
                                    }));
    int identifier = m_idle.addTimeout(secs * 1000);

    m_registeredActionTimeouts[action].append(identifier);

    return identifier;
}

void IdleManager::unregisterActionTimeout(Action *action, int identifier)
{
    auto it = m_registeredActionTimeouts.find(action);
    if (it == m_registeredActionTimeouts.end() || !it->removeOne(identifier))
        return;

    m_idle.removeTimeout(identifier);
}

void IdleManager::unregisterActionTimeouts(Action *action)
{
    const QList<int> timeoutsToClean = m_registeredActionTimeouts.take(action);
    disconnect(m_destroyConnections.take(action));

    for (int id : timeoutsToClean) {
        m_idle.removeTimeout(id);
    }

    m_pendingResumeFromIdleActions.remove(action);
}

void IdleManager::onIdleTimeoutReached(int identifier, int timeout)
{
    for (auto i = m_registeredActionTimeouts.constBegin(),
              end = m_registeredActionTimeouts.constEnd();
         i != end; ++i) {
        if (i.value().contains(identifier)) {
            auto *action = i.key();
            m_pendingResumeFromIdleActions.insert(action);
            action->onIdleTimeout(timeout);

            break;
        }
    }
}

void IdleManager::onResumingFromIdle()
{
    QList<QPointer<Action>> pending;
    for (auto *action : std::as_const(m_pendingResumeFromIdleActions))
        pending.append(action);
    m_pendingResumeFromIdleActions.clear();
    for (const auto &action : pending) {
        if (action)
            action->onWakeupFromIdle();
    }
}
