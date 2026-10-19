// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CHROMIUMSWITCHES_H
#define CHROMIUMSWITCHES_H

#include <string>
#include <vector>

// Whether the program is a Chromium build (a browser, Electron or CEF application), judged by
// the resources shipped beside its binary.
bool isChromiumProgram(const std::string &program);

// The command with the switches Chromium-based programs are started with.
std::vector<std::string> withChromiumSwitches(const std::vector<std::string> &command);

#endif
