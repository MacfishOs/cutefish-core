// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "privacyadaptor.h"
#include "privacyservice.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDebug>

// The udev rules test the files in here; systemd's StateDirectory creates it.
static const QString kStateDirectory = QStringLiteral("/var/lib/cutefish-privacy");

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("cutefish-privacy"));

    PrivacyService service(kStateDirectory);
    new PrivacyAdaptor(&service);

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.registerObject(QStringLiteral("/com/cutefish/Privacy"), &service)
        || !bus.registerService(QStringLiteral("com.cutefish.Privacy"))) {
        qWarning() << "cutefish-privacy: cannot register on the system bus";
        return 1;
    }

    QObject::connect(&service, &PrivacyService::idle, &app, &QCoreApplication::quit);

    return app.exec();
}
