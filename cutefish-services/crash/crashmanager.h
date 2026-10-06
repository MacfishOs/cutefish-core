// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "coredump.h"

#include <QHash>
#include <QObject>
#include <functional>

class CoredumpJournal;
class CrashDialog;
class QQmlEngine;

// Shows a dialog when an application the runtime started or a process the
// session restarts dumps core. One dialog per application or process; a
// repeat crash updates the open one.
class CrashManager : public QObject
{
    Q_OBJECT

public:
    CrashManager(std::function<QQmlEngine *()> engine, std::function<QString()> fixedFont,
                 QObject *parent = nullptr);
    ~CrashManager() override;

private:
    void onCoredump(const Coredump &dump);
    CrashDialog *dialogFor(const QString &key, bool application, const QString &id);

    std::function<QQmlEngine *()> m_engine;
    std::function<QString()> m_fixedFont;
    CoredumpJournal *m_journal;
    QHash<QString, CrashDialog *> m_dialogs;
};
