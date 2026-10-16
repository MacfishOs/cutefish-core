// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "base/logging.h"

inline const cutefish::base::LogCategory& sessionLog()
{
    static const cutefish::base::LogCategory category("cutefish.session");
    return category;
}
