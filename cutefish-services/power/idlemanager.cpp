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

void IdleManager::registerActionTimeout(Action *action, int secs)
{
    if (secs < 0 || secs > INT_MAX / 1000)
        return;
    if (!m_registeredActionTimeouts.contains(action))
        m_destroyConnections.insert(action,
                                    connect(action, &QObject::destroyed, this, [this, action] {
                                        unregisterActionTimeouts(action);
                                    }));
    int identifier = m_idle.addTimeout(secs * 1000);

    QList<int> timeouts = m_registeredActionTimeouts[action];
    timeouts.append(identifier);

    m_registeredActionTimeouts[action] = timeouts;
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
