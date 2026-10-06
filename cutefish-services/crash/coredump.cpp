// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "coredump.h"

#include <QRegularExpression>

#include <cxxabi.h>
#include <cstdlib>
#include <cstring>

static const QLatin1String s_scopePrefix("app-cutefish-");
static const QLatin1String s_scopeSuffix(".scope");

// The scope name without its prefix and suffix: <escaped id>-<pid>.
static QString scopeInstance(const QString &unit)
{
    if (!unit.startsWith(s_scopePrefix) || !unit.endsWith(s_scopeSuffix))
        return QString();
    return unit.mid(s_scopePrefix.size(), unit.size() - s_scopePrefix.size() - s_scopeSuffix.size());
}

// Reverses systemd's unit name escaping, \xNN for each byte of UTF-8.
static QString unescape(const QString &text)
{
    const QByteArray bytes = text.toLatin1();
    QByteArray result;
    for (int i = 0; i < bytes.size(); ++i) {
        if (bytes.at(i) == '\\' && i + 3 < bytes.size() && bytes.at(i + 1) == 'x') {
            bool ok = false;
            const int byte = bytes.mid(i + 2, 2).toInt(&ok, 16);
            if (ok) {
                result.append(char(byte));
                i += 3;
                continue;
            }
        }
        result.append(bytes.at(i));
    }
    return QString::fromUtf8(result);
}

QString Coredump::applicationId() const
{
    const QString instance = scopeInstance(userUnit);
    const int dash = instance.lastIndexOf(QLatin1Char('-'));
    if (dash <= 0)
        return QString();
    return unescape(instance.left(dash));
}

uint Coredump::applicationPid() const
{
    const QString instance = scopeInstance(userUnit);
    const int dash = instance.lastIndexOf(QLatin1Char('-'));
    if (dash <= 0)
        return 0;
    return instance.mid(dash + 1).toUInt();
}

QString Coredump::signalDescription() const
{
    const QString description = signal > 0 ? QString::fromLocal8Bit(::strsignal(signal)) : QString();
    if (signalName.isEmpty())
        return description;
    if (description.isEmpty())
        return signalName;
    return QStringLiteral("%1 (%2)").arg(description, signalName);
}

QString Coredump::report(const QString &name) const
{
    QString text;
    text += QStringLiteral("%1 quit unexpectedly\n").arg(name);
    text += QStringLiteral("Executable: %1\n").arg(executable);
    if (!commandLine.isEmpty())
        text += QStringLiteral("Command line: %1\n").arg(commandLine);
    text += QStringLiteral("Signal: %1\n").arg(signalDescription());
    if (time.isValid())
        text += QStringLiteral("Time: %1\n").arg(time.toString(Qt::ISODate));
    for (const CoredumpThread &thread : threads) {
        text += QStringLiteral("\nThread %1:\n").arg(thread.id);
        for (int i = 0; i < thread.frames.size(); ++i) {
            const CoredumpFrame &frame = thread.frames.at(i);
            text += QStringLiteral("#%1 %2 (%3)\n")
                            .arg(i)
                            .arg(frame.function.isEmpty() ? QStringLiteral("??") : frame.function,
                                 frame.module);
        }
    }
    return text;
}

QList<CoredumpThread> Coredump::parseStackTrace(const QString &message)
{
    static const QRegularExpression threadLine(QStringLiteral("^Stack trace of thread (\\d+):$"));
    // #3  0x00007fe7198dbede name (module + 0xdbede); the module part is
    // missing for an address no mapping covers.
    static const QRegularExpression frameLine(
            QStringLiteral("^#\\d+\\s+0x[0-9a-fA-F]+\\s+(\\S+)(?:\\s+\\((.*?)(?: \\+ 0x[0-9a-fA-F]+)?\\))?$"));

    QList<CoredumpThread> threads;
    const QStringList lines = message.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        const auto thread = threadLine.match(line);
        if (thread.hasMatch()) {
            threads.append({thread.captured(1), {}});
            continue;
        }
        if (threads.isEmpty())
            continue;
        const auto frame = frameLine.match(line);
        if (!frame.hasMatch())
            continue;
        const QString symbol = frame.captured(1);
        QString module = frame.captured(2);
        if (module == QLatin1String("n/a"))
            module.clear();
        threads.last().frames.append(
                {symbol == QLatin1String("n/a") ? QString() : demangle(symbol), module});
    }
    return threads;
}

QString Coredump::demangle(const QString &symbol)
{
    if (!symbol.startsWith(QLatin1String("_Z")))
        return symbol;
    int status = 0;
    char *name = abi::__cxa_demangle(symbol.toLatin1().constData(), nullptr, nullptr, &status);
    if (status != 0 || !name)
        return symbol;
    const QString result = QString::fromUtf8(name);
    std::free(name);
    return result;
}
