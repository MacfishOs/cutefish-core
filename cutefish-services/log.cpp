// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "log.h"

namespace services {

const cutefish::base::LogCategory &log()
{
    static const cutefish::base::LogCategory category("cutefish.services");
    return category;
}

} // namespace services
