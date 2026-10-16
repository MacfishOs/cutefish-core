// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "chromiumswitches.h"

#include "base/paths.h"

#include <filesystem>

namespace fs = std::filesystem;

static bool pathExists(const fs::path &path)
{
    std::error_code error;
    return fs::exists(path, error);
}

static bool hasChromiumResources(const fs::path &directory)
{
    return pathExists(directory / "icudtl.dat") && pathExists(directory / "chrome_100_percent.pak");
}

bool isChromiumProgram(const std::string &program)
{
    const std::string path = program.find('/') != std::string::npos
            ? program
            : cutefish::base::paths::findExecutable(program);
    if (path.empty())
        return false;

    std::error_code error;
    const fs::path binary = fs::canonical(path, error);
    if (error)
        return false;

    // Distribution wrapper scripts in /usr/bin run the real binary from /usr/lib/<name>.
    return hasChromiumResources(binary.parent_path())
        || hasChromiumResources(fs::path("/usr/lib") / binary.filename());
}

std::vector<std::string> withChromiumSwitches(const std::vector<std::string> &command)
{
    if (command.empty() || !isChromiumProgram(command.front()))
        return command;

    // Above scale 1 Chromium positions glyphs at fractions of a pixel and turns hinting off,
    // which leaves text soft at fractional scales; this keeps fontconfig's hinting.
    std::vector<std::string> result = command;
    result.insert(result.begin() + 1, "--disable-font-subpixel-positioning");
    return result;
}
