// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "userdirectories.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSaveFile>

void UserDirectories::ensure(const QString& home, const QString& configHome)
{
    const QString file = configHome + QStringLiteral("/user-dirs.dirs");
    if (home.isEmpty() || configHome.isEmpty() || QFile::exists(file))
        return;

    static const QList<QPair<QString, QString>> directories = {
            {QStringLiteral("DESKTOP"), QStringLiteral("Desktop")},
            {QStringLiteral("DOWNLOAD"), QStringLiteral("Downloads")},
            {QStringLiteral("TEMPLATES"), QStringLiteral("Templates")},
            {QStringLiteral("PUBLICSHARE"), QStringLiteral("Public")},
            {QStringLiteral("DOCUMENTS"), QStringLiteral("Documents")},
            {QStringLiteral("MUSIC"), QStringLiteral("Music")},
            {QStringLiteral("PICTURES"), QStringLiteral("Pictures")},
            {QStringLiteral("VIDEOS"), QStringLiteral("Videos")},
    };

    QByteArray contents;
    const QDir homeDir(home);
    for (const auto& [key, name] : directories) {
        // A directory that cannot be made stays unset, so lookups fall back to home.
        if (!homeDir.mkpath(name)) {
            qWarning() << "Could not create" << homeDir.filePath(name);
            continue;
        }
        contents += QStringLiteral("XDG_%1_DIR=\"$HOME/%2\"\n").arg(key, name).toUtf8();
    }

    QSaveFile output(file);
    if (!QDir().mkpath(configHome) || !output.open(QIODevice::WriteOnly)
        || output.write(contents) != contents.size() || !output.commit())
        qWarning() << "Could not write" << file;
}
