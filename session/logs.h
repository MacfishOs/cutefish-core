// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "base/logging.h"

inline const cutefish::base::LogCategory& sessionLog()
{
    static const cutefish::base::LogCategory category("cutefish.session");
    return category;
}
