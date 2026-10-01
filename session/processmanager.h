/*
 * Copyright (C) 2021 CutefishOS Team.
 *
 * Author:     revenmartin <revenmartin@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PROCESSMANAGER_H
#define PROCESSMANAGER_H

#include <QElapsedTimer>
#include <QEventLoop>
#include <QMap>
#include <functional>
#include <QObject>
#include <QProcess>

class XwaylandEnvironment;
class Application;
class QDBusServiceWatcher;
class CompositorProcess;
class ShellConnection;
class ShellProcess;
class QTimer;

class ProcessManager : public QObject
{
    Q_OBJECT

public:
    explicit ProcessManager(Application* app, QObject* parent = nullptr);
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
    // and the user is told which.
    void endAfterApplications(Ending ending, std::function<void()> then);

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
    void stopProcesses(QMap<QString, QProcess*>& processes);

private:
    XwaylandEnvironment* m_xwaylandEnvironment = nullptr;
    Application* m_app;
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
