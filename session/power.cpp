// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "power.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDebug>

namespace {

bool callLogind(const QString& method)
{
    QDBusMessage message = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
            QStringLiteral("org.freedesktop.login1.Manager"), method);
    // Interactive: polkit may ask for a password instead of refusing outright.
    message << true;

    const QDBusMessage reply = QDBusConnection::systemBus().call(message);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "logind" << method << "failed:" << reply.errorName() << reply.errorMessage();
        return false;
    }

    return true;
}

} // namespace

namespace Power {

bool reboot()
{
    return callLogind(QStringLiteral("Reboot"));
}

bool powerOff()
{
    return callLogind(QStringLiteral("PowerOff"));
}

bool suspend()
{
    return callLogind(QStringLiteral("Suspend"));
}

} // namespace Power
