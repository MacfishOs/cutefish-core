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

#ifndef LANGUAGE_H
#define LANGUAGE_H

#include <QObject>
#include <QSettings>

class Language : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString languageCode READ languageCode WRITE setLanguage NOTIFY languageChanged)

    Q_PROPERTY(QStringList preferredLanguages READ preferredLanguages WRITE setPreferredLanguages NOTIFY languageChanged)

public:
    static Language *self();
    Language(QObject *parent = nullptr);

    QString languageCode() const;
    void setLanguage(const QString &code);
    QStringList preferredLanguages() const;
    void setPreferredLanguages(const QStringList &codes);

signals:
    void languageChanged();

private:
    QSettings *m_settings;
};

#endif
