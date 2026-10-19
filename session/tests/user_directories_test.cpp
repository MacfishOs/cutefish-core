// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include <algorithm>
#include <filesystem>

#include <gtest/gtest.h>

#include "base/file.h"
#include "base/paths.h"
#include "testutil.h"
#include "userdirectories.h"

using cutefish::base::file::TemporaryDirectory;
namespace fs = std::filesystem;

static std::vector<std::string> directoryNames(const std::string& path)
{
    std::vector<std::string> names;
    for (const auto& item : fs::directory_iterator(path)) {
        if (item.is_directory())
            names.push_back(item.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

TEST(UserDirectories, firstLogin)
{
    TemporaryDirectory home(sessiontest::temporaryParent());
    const auto config = home.path() + "/.config";
    UserDirectories::ensure(home.path(), config);

    const std::vector<std::string> names {".config",  "Desktop",  "Documents", "Downloads", "Music",
                                          "Pictures", "Public",   "Templates", "Videos"};
    EXPECT_EQ(directoryNames(home.path()), names);

    EXPECT_EQ(sessiontest::readFile(config + "/user-dirs.dirs"),
              "XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
              "XDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\n"
              "XDG_TEMPLATES_DIR=\"$HOME/Templates\"\n"
              "XDG_PUBLICSHARE_DIR=\"$HOME/Public\"\n"
              "XDG_DOCUMENTS_DIR=\"$HOME/Documents\"\n"
              "XDG_MUSIC_DIR=\"$HOME/Music\"\n"
              "XDG_PICTURES_DIR=\"$HOME/Pictures\"\n"
              "XDG_VIDEOS_DIR=\"$HOME/Videos\"\n");

    // What the rest of the desktop reads back, $HOME being the user's home.
    EXPECT_EQ(cutefish::base::paths::userDirectory(cutefish::base::paths::UserDirectory::Downloads,
                                                   config + "/user-dirs.dirs"),
              cutefish::base::paths::home() + "/Downloads");
}

TEST(UserDirectories, existingFileIsKept)
{
    TemporaryDirectory home(sessiontest::temporaryParent());
    const auto config = home.path() + "/.config";
    const std::string contents = "XDG_DESKTOP_DIR=\"$HOME/Schreibtisch\"\n";
    ASSERT_TRUE(sessiontest::writeFile(config + "/user-dirs.dirs", contents));

    UserDirectories::ensure(home.path(), config);

    EXPECT_EQ(directoryNames(home.path()), std::vector<std::string> {".config"});
    EXPECT_EQ(sessiontest::readFile(config + "/user-dirs.dirs"), contents);
}
