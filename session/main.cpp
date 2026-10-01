// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application.h"

int main(int argc, char* argv[])
{
    Application a(argc, argv);

    return a.exec();
}
