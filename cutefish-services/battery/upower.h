// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#ifndef UPOWER_H
#define UPOWER_H

#include <cstdint>

namespace upower {

constexpr auto service = "org.freedesktop.UPower";
constexpr auto path = "/org/freedesktop/UPower";
constexpr auto interface = "org.freedesktop.UPower";
constexpr auto deviceInterface = "org.freedesktop.UPower.Device";

// The Device Type property's value for a battery that powers the computer.
constexpr uint32_t batteryType = 2;

// The Device State property.
enum class State : uint32_t {
    Unknown = 0,
    Charging = 1,
    Discharging = 2,
    Empty = 3,
    FullyCharged = 4,
    PendingCharge = 5,
    PendingDischarge = 6,
};

} // namespace upower

#endif // UPOWER_H
