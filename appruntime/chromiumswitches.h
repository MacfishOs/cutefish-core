// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CHROMIUMSWITCHES_H
#define CHROMIUMSWITCHES_H

#include <QStringList>

// Whether the program is a Chromium build (a browser, Electron or CEF application), judged by
// the resources shipped beside its binary.
bool isChromiumProgram(const QString &program);

// The command with the switches Chromium-based programs are started with.
QStringList withChromiumSwitches(const QStringList &command);

#endif
