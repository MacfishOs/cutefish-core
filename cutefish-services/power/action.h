// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2010 Dario Freddi <drf@kde.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CUTEFISH_SETTINGS_DAEMON_ACTION_H
#define CUTEFISH_SETTINGS_DAEMON_ACTION_H

#include <QObject>
#include <QVariantMap>

class Q_DECL_EXPORT Action : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(Action)

public:
    /**
     * Default constructor
     */
    explicit Action(QObject *parent);
    /**
     * Default destructor
     */
    ~Action() override;

    /**
     * This function is meant to find out if this action is available on this system. Actions
     * CAN reimplement this function if they are dependent on specific hardware/software
     * requirements. By default, this function will always return true.
     *
     * Should this function return false, the core will delete and ignore the action right after
     * creation.
     *
     * @returns Whether this action is supported or not by the current system
     */
    virtual bool isSupported();

protected:
    // Registration uses seconds; onIdleTimeout receives milliseconds.
    int registerIdleTimeout(int seconds);

    void unregisterIdleTimeout(int identifier);

public Q_SLOTS:
    /**
     * This slot is triggered whenever an idle timeout registered with registerIdleTimeout is
     * reached.
     *
     * @param msec The idle timeout reached in milliseconds
     */
    virtual void onIdleTimeout(int msec) = 0;
    /**
     * This slot is triggered whenever the PC wakes up from an Idle state. It is @b always called
     * after a registered idle timeout has been reached.
     */
    virtual void onWakeupFromIdle() = 0;

Q_SIGNALS:
    void actionTriggered(bool result, const QString &error = QString());
};

#endif // CUTEFISH_SETTINGS_DAEMON_ACTION_H
