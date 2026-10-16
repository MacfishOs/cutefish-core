// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DBUSCALL_H
#define DBUSCALL_H

#include <functional>
#include <memory>
#include <string>

#include "base/dbus.h"

namespace services {

// Calls without keeping the pending call: `done` runs once the answer came,
// however long the caller lives. Capture only what outlives the call.
template<typename... Args>
void callDetached(const cutefish::base::dbus::Proxy &proxy, const std::string &method,
                  std::function<void(const cutefish::base::dbus::Reply &)> done, const Args &...args)
{
    auto call = std::make_shared<cutefish::base::dbus::PendingCall>();
    *call = proxy.callAsync(
            method,
            [call, done = std::move(done)](const cutefish::base::dbus::Reply &reply) {
                if (done)
                    done(reply);
                // The bus keeps the call until this returns.
                call->reset();
            },
            args...);
}

} // namespace services

#endif // DBUSCALL_H
