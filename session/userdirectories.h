// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>

// The XDG user directories (Desktop, Downloads, ...), in place of xdg-user-dirs-update.
class UserDirectories final
{
public:
    // Creates the directories under English names and writes user-dirs.dirs when that file
    // is missing; an existing file is left as the user has it. Names on disk never follow
    // the language, the file manager shows them translated.
    static void ensure(const std::string& home, const std::string& configHome);
};
