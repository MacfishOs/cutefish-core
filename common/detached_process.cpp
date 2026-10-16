// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/detached_process.h"

#include "base/event_loop.h"

#include <memory>
#include <signal.h>
#include <spawn.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace cutefish::core {

pid_t startDetached(const std::vector<std::string>& command, const std::string& workingDirectory,
                    const Environment& environment)
{
    if (command.empty() || command.front().empty())
        return -1;

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    // The program starts with no signal blocked and default handlers, whatever
    // this process changed.
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attributes, &none);
    sigset_t defaults;
    sigfillset(&defaults);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSID);
    if (!workingDirectory.empty())
        posix_spawn_file_actions_addchdir_np(&actions, workingDirectory.c_str());

    std::vector<char*> argv;
    for (const std::string& argument : command)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    const std::vector<std::string> entries = environmentEntries(environment);
    std::vector<char*> envp;
    for (const std::string& entry : entries)
        envp.push_back(const_cast<char*>(entry.c_str()));
    envp.push_back(nullptr);

    pid_t pid = -1;
    const int result = posix_spawnp(&pid, argv.front(), &actions, &attributes, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (result != 0)
        return -1;

    // Reaped when it ends, so it never lingers as a zombie; after this
    // process, init does it.
    const int pidfd = int(syscall(SYS_pidfd_open, pid, 0));
    base::EventLoop* loop = base::EventLoop::current();
    if (pidfd >= 0 && loop) {
        auto watch = std::make_shared<int>(0);
        *watch = loop->watch(pidfd, EPOLLIN, [loop, pidfd, watch](uint32_t) {
            siginfo_t info{};
            if (waitid(P_PIDFD, id_t(pidfd), &info, WEXITED | WNOHANG) != 0 || info.si_pid == 0)
                return;
            loop->unwatch(*watch);
            ::close(pidfd);
        });
    } else if (pidfd >= 0) {
        ::close(pidfd);
    }
    return pid;
}

} // namespace cutefish::core
