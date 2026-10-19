// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "base/event_loop.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <poll.h>
#include <string>

namespace sessiontest {

// Runs the loop until the condition holds or the time is up; the condition's last answer.
inline bool runUntil(cutefish::base::EventLoop& loop, const std::function<bool()>& condition,
                     int milliseconds = 5000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return condition();
        pollfd fd {loop.fd(), POLLIN, 0};
        ::poll(&fd, 1, 10);
        loop.dispatch();
    }
    return true;
}

// Runs the loop for a while, whatever happens.
inline void runFor(cutefish::base::EventLoop& loop, int milliseconds)
{
    runUntil(loop, [] { return false; }, milliseconds);
}

inline std::string temporaryParent()
{
    return std::filesystem::temp_directory_path().string();
}

inline bool writeFile(const std::string& path, const std::string& contents)
{
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << contents;
    return bool(file);
}

inline std::string readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

} // namespace sessiontest
