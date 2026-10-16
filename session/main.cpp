// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application.h"

int main(int argc, char* argv[])
{
    cutefish::base::EventLoop loop;
    Application a(argc, argv, loop);

    return a.exec();
}
