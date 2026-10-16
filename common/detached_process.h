// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <sys/types.h>
#include <vector>

#include "common/environment.h"

namespace cutefish::core {

// Runs `command` (the program, looked up on PATH, and its arguments) in a
// session of its own with exactly `environment`; its end is collected on the
// calling thread's loop. The program's id, or -1 when it cannot be started.
pid_t startDetached(const std::vector<std::string>& command, const std::string& workingDirectory,
                    const Environment& environment);

} // namespace cutefish::core
