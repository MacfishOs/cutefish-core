// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>

static QJsonArray toArray(const QStringList &list)
{
    return QJsonArray::fromStringList(list);
}

static QString readProc(const QString &name)
{
    QFile file(QStringLiteral("/proc/self/") + name);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    return QString::fromLocal8Bit(file.readAll()).trimmed();
}

// Reports how the launched application sees itself on stdout.
extern "C" int cutefish_application_main(int argc, char *argv[])
{
    QStringList argvList;
    for (int i = 0; i < argc; ++i)
        argvList.append(QString::fromLocal8Bit(argv[i]));

    QString cmdline = readProc(QStringLiteral("cmdline"));
    cmdline.replace(QChar(0), QChar(' '));

    const QJsonObject report{
        {QStringLiteral("hasApplication"), qGuiApp != nullptr},
        {QStringLiteral("argv"), toArray(argvList)},
        {QStringLiteral("arguments"), toArray(QCoreApplication::arguments())},
        {QStringLiteral("applicationFilePath"), QCoreApplication::applicationFilePath()},
        {QStringLiteral("applicationDirPath"), QCoreApplication::applicationDirPath()},
        {QStringLiteral("applicationName"), QCoreApplication::applicationName()},
        {QStringLiteral("currentPath"), QDir::currentPath()},
        {QStringLiteral("activationToken"), qEnvironmentVariable("XDG_ACTIVATION_TOKEN")},
        {QStringLiteral("cmdline"), cmdline.trimmed()},
        {QStringLiteral("comm"), readProc(QStringLiteral("comm"))},
    };

    const QByteArray json = QJsonDocument(report).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(), 1, json.size(), stdout);
    std::fflush(stdout);
    return 7;
}
