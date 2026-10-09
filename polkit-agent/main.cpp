// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QFile>
#include <QGuiApplication>
#include <QLocale>
#include <QTranslator>

#include "cutefishapplication.h"
#include "polkitagentlistener.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    Cutefish::Application::prepare();
    app.setQuitOnLastWindowClosed(false);

    // Translations
    QLocale locale;
    QString qmFilePath = QString("%1/%2.qm")
                                 .arg("/usr/share/cutefish-polkit-agent/translations/")
                                 .arg(locale.name());
    if (QFile::exists(qmFilePath)) {
        QTranslator *translator = new QTranslator(QGuiApplication::instance());
        if (translator->load(qmFilePath)) {
            QGuiApplication::installTranslator(translator);
        } else {
            translator->deleteLater();
        }
    }

    PolKitAgentListener listener;

    if (!listener.registerAgent(QStringLiteral("/com/cutefish/PolicyKit1/AuthenticationAgent")))
        return -1;

    return app.exec();
}
