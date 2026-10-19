// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "power.h"

#include "base/dbus.h"
#include "logs.h"

namespace dbus = cutefish::base::dbus;

static bool callLogind(const std::string& method)
{
    dbus::Connection* bus = dbus::Connection::system();
    if (!bus) {
        CF_WARNING(sessionLog()) << "logind" << method << "failed: no system bus";
        return false;
    }
    dbus::Message message = bus->newMethodCall("org.freedesktop.login1", "/org/freedesktop/login1",
                                               "org.freedesktop.login1.Manager", method);
    // Interactive: polkit may ask for a password instead of refusing outright.
    message.append(true);

    const dbus::Reply reply = bus->call(message);
    if (reply.isError()) {
        CF_WARNING(sessionLog()) << "logind" << method << "failed:" << reply.error().name
                                 << reply.error().message;
        return false;
    }

    return true;
}

namespace Power {

bool reboot()
{
    return callLogind("Reboot");
}

bool powerOff()
{
    return callLogind("PowerOff");
}

bool suspend()
{
    return callLogind("Suspend");
}

} // namespace Power
