// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BOOSTER_H
#define BOOSTER_H

#include <QObject>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTimer>

class QSocketNotifier;

/**
 * Keeps one cutefish-booster waiting: a process that has constructed the
 * application object and loaded FishUI before it knows which application it
 * will become. An application built with cutefish_add_application() starts in
 * it instead of in a new process, which saves it the toolkit's start-up.
 */
class Booster : public QObject
{
    Q_OBJECT

public:
    explicit Booster(QObject *parent = nullptr);
    ~Booster() override;

    // The pid the command now runs as, or 0 when it has to be started as a new
    // process. environment is what a new process would get, without the token.
    uint launch(const QStringList &command, const QString &workingDirectory,
                const QProcessEnvironment &environment, const QString &activationToken);

private:
    void spawn();
    void discard();
    void readReady();

    static QString moduleFor(const QString &executable);

    // What the waiting booster was started with; it read its locale from it.
    QProcessEnvironment m_environment;
    QSocketNotifier *m_notifier = nullptr;
    int m_socket = -1;
    uint m_pid = 0;
    bool m_ready = false;
    int m_failures = 0;
    QTimer m_spawnTimer;
};

#endif // BOOSTER_H
