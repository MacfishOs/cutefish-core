// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application.h"

#include "ui/application.h"

int main(int argc, char *argv[])
{
    cutefish::ui::Application app(argc, argv);
    app.setApplicationId("cutefish-services");
    Application services;
    return app.run();
}
