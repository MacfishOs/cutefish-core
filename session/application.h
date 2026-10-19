// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#ifndef APPLICATION_H
#define APPLICATION_H

#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/event_loop.h"
#include "base/session_language.h"
#include "base/timer.h"

#include "processmanager.h"

class Application
{
public:
    Application(int argc, char** argv, cutefish::base::EventLoop& loop);
    ~Application();

    int exec() { return m_loop.run(); }

    void logout() { m_processManager->logout(); }
    void reboot();
    void powerOff();
    bool suspend();
    void startDesktopProcess() { m_processManager->startDesktopProcess(); }

private:
    void endSessionOnTerminationSignals();
    void exportSession();
    void initEnvironments();
    void initLanguage();
    bool syncDBusEnvironment();
    void importSystemdEnvironment();
    void createConfigDirectory();
    int runSync(const std::string& program, const std::vector<std::string>& args);

private:
    cutefish::base::EventLoop& m_loop;
    std::unique_ptr<ProcessManager> m_processManager;
    std::unique_ptr<cutefish::base::dbus::Object> m_sessionObject;
    std::unique_ptr<cutefish::base::SessionLanguage> m_language;
    cutefish::base::Timer m_startTimer;
    int m_terminationWatch = 0;
};

#endif // APPLICATION_H
