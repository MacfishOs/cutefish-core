// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "process.h"

Process::Process(QObject* parent)
    : QProcess(parent)
{
    QProcess::setProcessChannelMode(QProcess::ForwardedChannels);
}

Process::~Process()
{
}
