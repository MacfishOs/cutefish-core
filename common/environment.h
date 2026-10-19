// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <map>
#include <string>
#include <vector>

namespace cutefish::core {

// A process environment by variable name.
using Environment = std::map<std::string, std::string>;

// This process's environment.
Environment processEnvironment();
// `NAME=value` entries, for base::Process::setEnvironment() and execve().
std::vector<std::string> environmentEntries(const Environment& environment);

} // namespace cutefish::core
