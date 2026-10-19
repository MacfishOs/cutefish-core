// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "userdirectories.h"

#include <filesystem>
#include <utility>

#include "base/file.h"
#include "logs.h"

namespace fs = std::filesystem;

void UserDirectories::ensure(const std::string& home, const std::string& configHome)
{
    const std::string file = configHome + "/user-dirs.dirs";
    std::error_code error;
    if (home.empty() || configHome.empty() || fs::exists(file, error))
        return;

    static const std::pair<const char*, const char*> directories[] = {
            {"DESKTOP", "Desktop"},
            {"DOWNLOAD", "Downloads"},
            {"TEMPLATES", "Templates"},
            {"PUBLICSHARE", "Public"},
            {"DOCUMENTS", "Documents"},
            {"MUSIC", "Music"},
            {"PICTURES", "Pictures"},
            {"VIDEOS", "Videos"},
    };

    std::string contents;
    for (const auto& [key, name] : directories) {
        const std::string path = home + '/' + name;
        // A directory that cannot be made stays unset, so lookups fall back to home.
        fs::create_directories(path, error);
        if (!fs::is_directory(path, error)) {
            CF_WARNING(sessionLog()) << "Could not create" << path;
            continue;
        }
        contents += std::string("XDG_") + key + "_DIR=\"$HOME/" + name + "\"\n";
    }

    if (!cutefish::base::file::writeAtomically(file, contents))
        CF_WARNING(sessionLog()) << "Could not write" << file;
}
