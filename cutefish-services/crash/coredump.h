// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

struct CoredumpFrame {
    QString function;
    QString module;
};

struct CoredumpThread {
    QString id;
    QList<CoredumpFrame> frames;
};

// One systemd-coredump journal entry. The stack trace is what systemd-coredump
// unwound when it took the dump: exported symbols only, no source lines.
struct Coredump {
    uint pid = 0;
    int signal = 0;
    QString signalName;
    QString executable;
    QString commandLine;
    QString userUnit;
    QString cgroup;
    QDateTime time;
    // The crashing thread comes first.
    QList<CoredumpThread> threads;

    // The desktop entry id of an application the runtime started, from the
    // app-cutefish-<id>-<pid>.scope it runs in; empty for anything else.
    QString applicationId() const;
    // The pid the runtime started the application with.
    uint applicationPid() const;

    QString signalDescription() const;
    // Plain text for the clipboard.
    QString report(const QString &name) const;

    static QList<CoredumpThread> parseStackTrace(const QString &message);
    static QString demangle(const QString &symbol);
};
