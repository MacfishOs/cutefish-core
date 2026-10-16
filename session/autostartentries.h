#pragma once

#include <string>
#include <vector>

// Directories are ordered from highest to lowest precedence.
std::vector<std::string> autostartEntries(const std::vector<std::string>& directories,
                                          const std::vector<std::string>& desktops);
