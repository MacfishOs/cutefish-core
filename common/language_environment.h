// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>
#include <vector>

#include "common/environment.h"

// The locale variables an application is started with, from the languages the
// user picked in the services' settings.
namespace cutefish::core::language {

// The language generated in /etc/locale.gen, until the user picks one.
std::string systemLanguage();
std::vector<std::string> preferredLanguages();
// POSIX names (ll_CC[@script]) for language codes such as zh_Hans_CN.
std::vector<std::string> translationLanguages(const std::vector<std::string>& codes);
void apply(Environment& environment, const std::vector<std::string>& codes);
void apply(Environment& environment);

} // namespace cutefish::core::language
