// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include "base/process.h"

// When this process writes to the journal, its output and errors move to a
// stream of its own under `identifier`; otherwise they stay where they are.
void useOwnJournalStream(const std::string &identifier);

// Starts a program whose output is forwarded, on a journal stream of its own
// named after the program, when this process writes to the journal.
bool startWithOwnJournalStream(cutefish::base::Process &process, const std::string &program,
                               const std::vector<std::string> &arguments = {});
