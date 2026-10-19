// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "base/dbus.h"
#include "base/process.h"
#include "base/timer.h"

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Generates the locales a user's language needs, for every user; an active
// local session asks without a password. Only UTF-8 locales the C library
// supports are made, as directories that programs already running find too,
// and enabled in the system's list of locales where it keeps one, so that
// regenerating them keeps them.
class LocalesService
{
public:
    struct Paths
    {
        std::string supported = "/usr/share/i18n/SUPPORTED";
        std::string sources = "/usr/share/i18n/locales";
        std::string aliases = "/usr/share/locale/locale.alias";
        std::string localeGen = "/etc/locale.gen";
        std::string output = "/usr/lib/locale";
        std::string localedef = "/usr/bin/localedef";
    };

    LocalesService();
    explicit LocalesService(Paths paths, std::chrono::milliseconds idleTimeout = std::chrono::seconds(60));
    ~LocalesService();

    // Exports org.cutefish.Locales at /org/cutefish/Locales on the bus; false
    // when the object cannot be added.
    bool exportTo(cutefish::base::dbus::Connection *bus);

    // Names such as "ja_JP.UTF-8" that can be generated here.
    bool isSupported(const std::string &name) const;
    bool isGenerated(const std::string &name) const;
    // Adds "<name> UTF-8" to the list, or enables it there; true when the
    // list has it now or there is no list.
    bool enableInLocaleGen(const std::string &name) const;

    // Nothing called for a while and nothing being generated.
    std::function<void()> onIdle;

private:
    struct Job
    {
        cutefish::base::dbus::MethodCall call;
        std::vector<std::string> names;
    };

    void generate(cutefish::base::dbus::MethodCall &call);
    void runNext();
    void finishJob(bool succeeded);

    Paths m_paths;
    cutefish::base::Timer m_idleTimer;
    cutefish::base::dbus::Connection *m_bus = nullptr;
    std::unique_ptr<cutefish::base::dbus::Object> m_object;
    std::deque<Job> m_jobs;
    std::unique_ptr<cutefish::base::Process> m_localedef;
    int m_pendingAuthorizations = 0;
};
