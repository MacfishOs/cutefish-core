// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RESTARTPOLICY_H
#define RESTARTPOLICY_H

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <functional>

class QTimer;

// Bounded restarts for a session process. The compositor keeps running either
// way: a session missing one of its processes is recoverable, a restart loop
// is not.
class RestartPolicy : public QObject
{
    Q_OBJECT

public:
    struct Limits {
        int maxRestarts = 5;
        // A process that stayed up this long was not crash-looping, so it gets a fresh count.
        qint64 stableUptimeMs = 30000;
        int delayMs = 1000;
        int backoffMs = 2000;
    };

    RestartPolicy(const QString &name, std::function<void()> restart, QObject *parent = nullptr);
    RestartPolicy(const QString &name, std::function<void()> restart, const Limits &limits,
                  QObject *parent = nullptr);

    // Call on every start, the first included.
    void started();
    void exited(int code, QProcess::ExitStatus status);
    // Cancels a pending restart; later exits are ignored.
    void stop();

    bool isPending() const;
    bool hasGivenUp() const;

private:
    QString m_name;
    std::function<void()> m_restart;
    Limits m_limits;
    QTimer *m_timer;
    QElapsedTimer m_uptime;
    int m_restarts = 0;
    bool m_stopped = false;
    bool m_gaveUp = false;
};

#endif // RESTARTPOLICY_H
