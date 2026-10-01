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
        m_processManager->endAfterApplications(ProcessManager::Ending::Restart, [this] {
            if (Power::reboot())
                QCoreApplication::exit(0);
        });
    }

    void powerOff()
    {
        m_processManager->endAfterApplications(ProcessManager::Ending::Shutdown, [this] {
            if (Power::powerOff())
                QCoreApplication::exit(0);
        });
    }

    bool suspend() { return Power::suspend(); }

    void startDesktopProcess() { m_processManager->startDesktopProcess(); }

private:
    void endSessionOnTerminationSignals();
    void initEnvironments();
    void initLanguage();
    bool syncDBusEnvironment();
    void importSystemdEnvrionment();
    void createConfigDirectory();
    void updateUserDirs();
    int runSync(const QString& program, const QStringList& args, const QStringList& env = {});

private:
    ProcessManager* m_processManager;
};

#endif // APPLICATION_H
