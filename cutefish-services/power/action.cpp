// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2010 Dario Freddi <drf@kde.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "action.h"
#include "idlemanager.h"

Action::Action(IdleManager *idle)
    : m_idle(idle)
{
}

Action::~Action()
{
    if (m_idle)
        m_idle->unregisterActionTimeouts(this);
}

int Action::registerIdleTimeout(int seconds)
{
    return m_idle ? m_idle->registerActionTimeout(this, seconds) : -1;
}

void Action::unregisterIdleTimeout(int identifier)
{
    if (m_idle)
        m_idle->unregisterActionTimeout(this, identifier);
}

bool Action::isSupported()
{
    return true;
}
