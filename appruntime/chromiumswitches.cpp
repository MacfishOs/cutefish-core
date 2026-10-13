// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "chromiumswitches.h"

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

static bool hasChromiumResources(const QString &directory)
{
    return QFile::exists(directory + QLatin1String("/icudtl.dat"))
        && QFile::exists(directory + QLatin1String("/chrome_100_percent.pak"));
}

bool isChromiumProgram(const QString &program)
{
    const QString path = program.contains(QLatin1Char('/'))
            ? program
            : QStandardPaths::findExecutable(program);
    if (path.isEmpty())
        return false;

    const QFileInfo binary(QFileInfo(path).canonicalFilePath());
    if (!binary.exists())
        return false;

    // Distribution wrapper scripts in /usr/bin run the real binary from /usr/lib/<name>.
    return hasChromiumResources(binary.absolutePath())
        || hasChromiumResources(QLatin1String("/usr/lib/") + binary.fileName());
}

QStringList withChromiumSwitches(const QStringList &command)
{
    if (command.isEmpty() || !isChromiumProgram(command.first()))
        return command;

    // Above scale 1 Chromium positions glyphs at fractions of a pixel and turns hinting off,
    // which leaves text soft at fractional scales; this keeps fontconfig's hinting.
    QStringList result = command;
    result.insert(1, QStringLiteral("--disable-font-subpixel-positioning"));
    return result;
}
