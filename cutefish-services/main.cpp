// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "application.h"

#include "base/i18n.h"
#include "ui/application.h"

int main(int argc, char *argv[])
{
    cutefish::ui::Application app(argc, argv);
    app.setApplicationId("cutefish-services");
    cutefish::i18n::setDomain("cutefish-core");
    Application services;
    return app.run();
}
