// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application.h"
#include "nightlightadaptor.h"
#include "poweradaptor.h"
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QQmlEngine>
#include <QTranslator>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

Application::Application(int &argc, char **argv)
    : QGuiApplication(argc, argv)
    , m_themeManager(ThemeManager::self())
    , m_brightnessManager(new BrightnessManager(this))
    , m_upowerManager(new UPowerManager(this))
    , m_batteryWarning(new BatteryWarning(this))
    , m_language(Language::self())
    , m_clipboard(new Clipboard(this))
    , m_powerManager(new PowerManager(m_upowerManager, this))
    , m_nightLight(new NightLight(this))
    , m_permissionManager(new PermissionManager([this] { return engine(); }, this))
    , m_crashManager(new CrashManager([this] { return engine(); },
                                      [this] { return m_themeManager->systemFixedFont(); }, this))
    , m_proxyService(new ProxyService(this))
{
    initTrash();

    // Register all service objects before publishing the well-known name.
    new PowerAdaptor(m_powerManager);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/com/cutefish/Services/Power"),
                                                 m_powerManager);
    new NightLightAdaptor(m_nightLight);
    QDBusConnection::sessionBus().registerObject(
            QStringLiteral("/com/cutefish/Services/NightLight"), m_nightLight);

    // Translations
    QLocale locale;
    QString qmFilePath = QString("%1/%2.qm")
                                 .arg("/usr/share/cutefish-services/translations/")
                                 .arg(locale.name());
    if (QFile::exists(qmFilePath)) {
        QTranslator *translator = new QTranslator(QGuiApplication::instance());
        if (translator->load(qmFilePath)) {
            QGuiApplication::installTranslator(translator);
        } else {
            translator->deleteLater();
        }
    }

    QDBusConnection::sessionBus().registerService(QStringLiteral("com.cutefish.Services"));
}

QQmlEngine *Application::engine()
{
    if (!m_engine)
        m_engine = new QQmlEngine(this);
    return m_engine;
}

void Application::initTrash()
{
    const QByteArray trashDir = QString(QDir::homePath() + "/.local/share/Trash").toLatin1();

    if (::mkdir(trashDir, 0700) != 0)
        return;

    struct stat buff;
    uid_t uid = getuid();

    if (::lstat(trashDir, &buff) != 0)
        return;

    if ((buff.st_uid == uid) && // must be owned by user
        ((buff.st_mode & 0777) == 0700)) {
        // check subdirs
        QString infoDir = trashDir + QString::fromLatin1("/info");
        QString filesDir = trashDir + QString::fromLatin1("/files");

        if (!QDir(infoDir).exists())
            QDir().mkdir(infoDir);

        if (!QDir(filesDir).exists())
            QDir().mkdir(filesDir);
    } else {
        ::rmdir(trashDir);
    }
}
