// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "restartpolicy.h"

#include <QDebug>
#include <QTimer>

RestartPolicy::RestartPolicy(const QString &name, std::function<void()> restart, QObject *parent)
    : RestartPolicy(name, std::move(restart), Limits(), parent)
{
}

RestartPolicy::RestartPolicy(const QString &name, std::function<void()> restart,
                             const Limits &limits, QObject *parent)
    : QObject(parent)
    , m_name(name)
    , m_restart(std::move(restart))
    , m_limits(limits)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (!m_stopped)
            m_restart();
    });
}

void RestartPolicy::started()
{
    m_uptime.start();
}

void RestartPolicy::exited(int code, QProcess::ExitStatus status)
{
    if (m_stopped || m_timer->isActive())
        return;

    if (m_uptime.isValid() && m_uptime.elapsed() >= m_limits.stableUptimeMs)
        m_restarts = 0;
    if (m_restarts >= m_limits.maxRestarts) {
        qCritical() << m_name << "failed" << m_restarts << "times; not restarting it again";
        m_gaveUp = true;
        return;
    }

    const int delay = m_limits.delayMs + m_restarts * m_limits.backoffMs;
    ++m_restarts;
    qWarning() << m_name << "exited" << code << status << "- restarting in" << delay << "ms";
    m_timer->start(delay);
}

void RestartPolicy::stop()
{
    m_stopped = true;
    m_timer->stop();
}

bool RestartPolicy::isPending() const
{
    return m_timer->isActive();
}

bool RestartPolicy::hasGivenUp() const
{
    return m_gaveUp;
}
