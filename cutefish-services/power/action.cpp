// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2010 Dario Freddi <drf@kde.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "action.h"
#include "idlemanager.h"

Action::Action(QObject* parent)
        : QObject(parent)
{
}

Action::~Action() = default;

int Action::registerIdleTimeout(int seconds)
{
    return IdleManager::self()->registerActionTimeout(this, seconds);
}

void Action::unregisterIdleTimeout(int identifier)
{
    IdleManager::self()->unregisterActionTimeout(this, identifier);
}

bool Action::isSupported()
{
    return true;
}

