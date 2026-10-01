// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

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
