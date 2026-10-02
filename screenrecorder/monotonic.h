// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CUTEFISH_RECORDER_MONOTONIC_H
#define CUTEFISH_RECORDER_MONOTONIC_H

#include <QtGlobal>

#include <ctime>

// The clock the compositor stamps its frames with.
inline qint64 monotonicNs()
{
    timespec now {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return qint64(now.tv_sec) * 1000000000 + now.tv_nsec;
}

#endif // CUTEFISH_RECORDER_MONOTONIC_H
