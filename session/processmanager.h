// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PROCESSMANAGER_H
#define PROCESSMANAGER_H

#include <QElapsedTimer>
#include <QMap>
#include <functional>
#include <QObject>
#include <QProcess>

class XwaylandEnvironment;
class QDBusServiceWatcher;
class CompositorProcess;
class ShellConnection;
class ShellProcess;
class QTimer;

class ProcessManager : public QObject
{
    Q_OBJECT

public:
    explicit ProcessManager(QObject* parent = nullptr);
    ~ProcessManager();

    enum class Ending {
        Logout,
        Restart,
        Shutdown,
    };

    void start();
    void logout();

    // Asks every application to quit, as Quit asks one, and runs `then` once
    // all have. One that cancels or stops responding keeps the session going,
    // and the user is told which. `then` returns false when the session goes on
    // after all, as after a refused restart.
    void endAfterApplications(Ending ending, std::function<bool()> then);

    // What logout does once the display manager has been told, for when it or
    // the system is the one ending the session.
    void endSession(bool behindLoginScreen = false);

    void startDesktopProcess();
    void startDaemonProcess();
    void loadAutoStartProcess();

private:
    void leave();
    void notifyEndingStopped(Ending ending, const QString &outcome, const QString &applicationId);
    void startAfterCompositorReady();
    void publishXwaylandEnvironment(bool startDesktop);
    void startShell();
    void onShellFinished(int code, QProcess::ExitStatus status);
    void stopShellAndDaemons();
    void stopProcesses(QMap<QString, QProcess*>& processes);

private:
    XwaylandEnvironment* m_xwaylandEnvironment = nullptr;
    CompositorProcess* m_compositorProcess = nullptr;
    ShellConnection* m_shellConnection = nullptr;
    ShellProcess* m_shellProcess = nullptr;
    QTimer* m_shellRestartTimer = nullptr;
    QDBusServiceWatcher* m_servicesWatcher = nullptr;
    bool m_started = false;
    bool m_compositorReady = false;
    bool m_desktopStarted = false;
    bool m_loggingOut = false;
    bool m_ending = false;
    // Bounded restart policy: a shell that keeps dying must not become a busy
    // loop, and after enough failures the session carries on without it.
    int m_shellRestarts = 0;
    QElapsedTimer m_shellUptime;
    QString m_waylandSocket;
    QString m_controlSocket;
    QMap<QString, QProcess*> m_coreProcesses;
};

#endif // PROCESSMANAGER_H
