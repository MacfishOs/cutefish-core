// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

//! System power actions, asked of systemd-logind. Each call blocks until logind
//! answers and returns whether it accepted the request.
namespace Power {

bool reboot();
bool powerOff();
bool suspend();

} // namespace Power
