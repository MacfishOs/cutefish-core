/*
 * Copyright (C) 2021 CutefishOS Team.
 *
 * Author:     revenmartin <revenmartin@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "language.h"
#include "languageenvironment.h"
#include "preferences.h"
#include "appearance/thememanager.h"
#include "languageadaptor.h"

#include <QDBusInterface>
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
    QStringList codes = m_settings->value("preferredLanguages").toStringList();
    codes.removeAll(languageCode());
    codes.prepend(languageCode());
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

    QDBusInterface iface("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                         "org.freedesktop.Notifications", QDBusConnection::sessionBus());
    if (iface.isValid()) {
        QList<QVariant> args;
        args << "cutefish-settings";
        args << ((unsigned int)0);
        args << "preferences-system";
        args << "";
        args << tr("Some applications may need to be restarted for the change to take effect.");
        args << QStringList();
        args << QVariantMap();
        args << (int)10;
        iface.asyncCallWithArgumentList("Notify", args);
    }
}
