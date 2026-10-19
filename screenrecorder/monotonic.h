// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CUTEFISH_RECORDER_MONOTONIC_H
#define CUTEFISH_RECORDER_MONOTONIC_H

#include <cstdint>
#include <ctime>

// The clock the compositor stamps its frames with.
inline int64_t monotonicNs()
{
    timespec now {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return int64_t(now.tv_sec) * 1000000000 + now.tv_nsec;
}

#endif // CUTEFISH_RECORDER_MONOTONIC_H
