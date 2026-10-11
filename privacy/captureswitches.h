// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QObject>

class QSocketNotifier;

// Every sound card's capture switches, turned off under ALSA element locks.
// Only the file that took a lock can write the element or release it, so no
// other process, the sound server included, can turn a microphone back on.
// The kernel drops the locks when this process exits.
class CaptureSwitches : public QObject
{
    Q_OBJECT

public:
    explicit CaptureSwitches(const QString &deviceDirectory = QStringLiteral("/dev/snd"),
                             QObject *parent = nullptr);
    ~CaptureSwitches() override;

    bool isLocked() const { return m_locked; }
    void setLocked(bool locked);

private:
    void lockCard(const QString &name);
    void unlockCard(const QString &name);
    void readEvents();

    QString m_deviceDirectory;
    bool m_locked = false;
    // Control node name to the open file holding its locks.
    QHash<QString, int> m_cards;
    int m_inotify = -1;
    QSocketNotifier *m_notifier = nullptr;
};
