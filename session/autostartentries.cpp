#include "autostartentries.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

QStringList autostartEntries(const QStringList& directories, const QStringList& desktops)
{
    QStringList result;
    QSet<QString> seen;
    for (const auto& directory : directories) {
        const QDir dir(directory);
        for (const auto& name : dir.entryList({"*.desktop"}, QDir::Files, QDir::Name)) {
            if (seen.contains(name))
                continue;
            // A disabled or invalid override must still mask lower-priority entries.
            seen.insert(name);
            QFile file(dir.filePath(name));
            if (!file.open(QIODevice::ReadOnly))
                continue;
            QTextStream stream(&file);
            QMap<QString, QString> entry;
            bool desktopGroup = false;
            while (!stream.atEnd()) {
                const auto line = stream.readLine().trimmed();
                if (line.startsWith('[')) {
                    desktopGroup = line == "[Desktop Entry]";
                    continue;
                }
                if (!desktopGroup || line.startsWith('#'))
                    continue;
                const auto equals = line.indexOf('=');
                if (equals > 0)
                    entry.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
            }
            if (entry.value("Hidden") == "true" || entry.value("Type") != "Application")
                continue;
            const auto matches = [&entry, &desktops](const QString& key) {
                const auto names = entry.value(key).split(';', Qt::SkipEmptyParts);
                for (const auto& desktop : desktops) {
                    if (names.contains(desktop))
                        return true;
                }
                return false;
            };
            if ((entry.contains("OnlyShowIn") && !matches("OnlyShowIn")) || matches("NotShowIn"))
                continue;
            const auto executable = entry.value("TryExec");
            if (!executable.isEmpty()) {
                if (QDir::isAbsolutePath(executable)) {
                    const QFileInfo info(executable);
                    if (!info.isFile() || !info.isExecutable())
                        continue;
                } else if (executable.contains('/') ||
                           QStandardPaths::findExecutable(executable).isEmpty()) {
                    continue;
                }
            }
            const auto command = entry.value("Exec");
            if (!command.trimmed().isEmpty())
                result.append(command);
        }
    }
    return result;
}
