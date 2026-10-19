// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <chrono>
#include <functional>

#include "base/dbus.h"

namespace cutefish::core {

// Calls without keeping the pending call: `done` runs once the answer came,
// however long the caller lives, or at once when the call cannot be sent. A
// connection that closes first drops it unanswered. Capture only what
// outlives the call.
void callDetached(base::dbus::Connection* connection, const base::dbus::Message& call,
                  std::function<void(const base::dbus::Reply&)> done, std::chrono::microseconds timeout = {});

} // namespace cutefish::core
