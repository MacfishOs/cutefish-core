// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2010 Dario Freddi <drf@kde.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IDLEMANAGER_H
#define IDLEMANAGER_H

#include <QHash>
#include <QObject>
#include <QSet>

#include "action.h"
#include "idle/waylandidle.h"

class IdleManager : public QObject
{
    Q_OBJECT

public:
    static IdleManager *self();
    explicit IdleManager(QObject *parent = nullptr);

    int registerActionTimeout(Action *action, int secs);
    void unregisterActionTimeout(Action *action, int identifier);
    void unregisterActionTimeouts(Action *action);

private slots:
    void onIdleTimeoutReached(int, int);
    void onResumingFromIdle();

private:
    friend class Action;

    WaylandIdle m_idle;
    QHash<Action *, QMetaObject::Connection> m_destroyConnections;
    QHash<Action *, QList<int>> m_registeredActionTimeouts;
    QSet<Action *> m_pendingResumeFromIdleActions;
};

#endif // IDLEMANAGER_H
