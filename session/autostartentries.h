#pragma once

#include <string>
#include <vector>

// What the session starts and restarts itself, so no autostart entry may start
// it again.
bool isStartedBySession(const std::string& executable);

// Directories are ordered from highest to lowest precedence.
std::vector<std::string> autostartEntries(const std::vector<std::string>& directories,
                                          const std::vector<std::string>& desktops);
