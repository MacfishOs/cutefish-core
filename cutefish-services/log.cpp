// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "log.h"

namespace services {

const cutefish::base::LogCategory &log()
{
    static const cutefish::base::LogCategory category("cutefish.services");
    return category;
}

} // namespace services
