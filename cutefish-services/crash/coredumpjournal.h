// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "coredump.h"

#include <QObject>

class QSocketNotifier;
class QTimer;
struct sd_journal;

// Follows systemd-coredump's journal entries for this user's processes from
// the moment it starts; earlier crashes are not reported.
class CoredumpJournal : public QObject
{
    Q_OBJECT

public:
    explicit CoredumpJournal(QObject *parent = nullptr);
    ~CoredumpJournal() override;

    bool isOpen() const { return m_journal != nullptr; }

Q_SIGNALS:
    void coredump(const Coredump &dump);

private:
    void readNewEntries();
    void scheduleTimeout();
    Coredump currentEntry() const;
    QString field(const char *name) const;

    sd_journal *m_journal = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    QTimer *m_timeout = nullptr;
};
