// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef APPLICATION_H
#define APPLICATION_H

#include <QCoreApplication>

#include "power.h"
#include "processmanager.h"

class Application : public QCoreApplication
{
    Q_OBJECT

public:
    explicit Application(int& argc, char** argv);
    ~Application() override;

public slots:

    void logout() { m_processManager->logout(); }

    void reboot()
    {
        m_processManager->endAfterApplications(ProcessManager::Ending::Restart, [] {
            const bool rebooting = Power::reboot();
            if (rebooting)
                QCoreApplication::exit(0);
            return rebooting;
        });
    }

    void powerOff()
    {
        m_processManager->endAfterApplications(ProcessManager::Ending::Shutdown, [] {
            const bool poweringOff = Power::powerOff();
            if (poweringOff)
                QCoreApplication::exit(0);
            return poweringOff;
        });
    }

    bool suspend() { return Power::suspend(); }

    void startDesktopProcess() { m_processManager->startDesktopProcess(); }

private:
    void endSessionOnTerminationSignals();
    void initEnvironments();
    void initLanguage();
    bool syncDBusEnvironment();
    void importSystemdEnvironment();
    void createConfigDirectory();
    int runSync(const QString& program, const QStringList& args);

private:
    ProcessManager* m_processManager;
};

#endif // APPLICATION_H
