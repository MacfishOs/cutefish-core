// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "language.h"
#include "languageenvironment.h"
#include "cutefishnotification.h"
#include "preferences.h"
#include "languageadaptor.h"

#include <QDBusConnection>
#include <QSettings>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QLocale>

Language *Language::self()
{
    static Language s;
    return &s;
}

Language::Language(QObject *parent)
    : QObject(parent)
    , m_settings(new QSettings(Preferences::file(QStringLiteral("services")), QSettings::IniFormat, this))
{
    m_settings->beginGroup(QStringLiteral("Language"));

    qDBusRegisterMetaType<QMap<QString, QString>>();
    new LanguageAdaptor(this);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/com/cutefish/Services/Language"),
                                                 this);

    emit languageChanged();
}

QString Language::languageCode() const
{
    const QString code = m_settings->value("language").toString();
    return code.isEmpty() ? LanguageEnvironment::systemLanguage() : code;
}

QStringList Language::preferredLanguages() const
{
    const QString primary = languageCode();
    QStringList codes = m_settings->value("preferredLanguages").toStringList();
    codes.removeAll(primary);
    codes.prepend(primary);
    codes.removeDuplicates();
    return codes;
}

void Language::setLanguage(const QString &code)
{
    QStringList codes = preferredLanguages();
    codes.removeAll(code);
    codes.prepend(code);
    setPreferredLanguages(codes);
}

void Language::setPreferredLanguages(const QStringList &languages)
{
    QStringList codes = languages;
    codes.removeAll(QString());
    codes.removeDuplicates();
    if (codes.isEmpty() || codes == preferredLanguages())
        return;

    const QString code = codes.first();
    const bool primaryChanged = code != languageCode();
    m_settings->setValue("preferredLanguages", codes);

    if (primaryChanged) {
        QSettings settings(Preferences::file(QStringLiteral("services")), QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("Appearance"));

        const QString localeName = QLocale(code).name();
        if (localeName == "zh_CN") {
            settings.setValue("Font", "Noto Sans CJK SC");
        } else if (localeName.startsWith("en_")) {
            settings.setValue("Font", "Noto Sans");
        } else if (localeName == "zh_TW") {
            settings.setValue("Font", "Noto Sans CJK TC");
        } else if (localeName == "zh_HK") {
            settings.setValue("Font", "Noto Serif CJK HK");
        } else if (localeName == "ja_JP") {
            settings.setValue("Font", "Noto Serif CJK JP");
        }
    }

    m_settings->setValue("language", code);
    m_settings->sync();

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    LanguageEnvironment::apply(environment, codes);
    QMap<QString, QString> activationEnvironment;
    QStringList systemdEnvironment;
    for (const QString &name : environment.keys()) {
        if (name == "LANG" || name == "LANGUAGE" || name.startsWith("LC_")) {
            activationEnvironment.insert(name, environment.value(name));
            systemdEnvironment.append(name + '=' + environment.value(name));
        }
    }
    // D-Bus cannot remove variables; an empty LC_ALL lets individual categories take effect.
    activationEnvironment.insert("LC_ALL", QString());
    auto dbusMessage = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "UpdateActivationEnvironment");
    dbusMessage << QVariant::fromValue(activationEnvironment);
    QDBusConnection::sessionBus().asyncCall(dbusMessage);

    auto systemdMessage = QDBusMessage::createMethodCall("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "UnsetAndSetEnvironment");
    systemdMessage.setAutoStartService(false);
    systemdMessage << QStringList{"LC_ALL"} << systemdEnvironment;
    QDBusConnection::sessionBus().asyncCall(systemdMessage);
    emit languageChanged();

    if (!primaryChanged)
        return;

    auto *notification = new Cutefish::Notification(this);
    notification->setDesktopEntry(QStringLiteral("cutefish-settings"));
    notification->setIconName(QStringLiteral("preferences-system"));
    notification->setBody(tr("Some applications may need to be restarted for the change to take effect."));
    notification->setTimeout(10);
    notification->setAutoDelete(true);
    notification->show();
}
