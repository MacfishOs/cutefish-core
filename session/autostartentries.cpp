#include "autostartentries.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <unistd.h>

#include "base/file.h"
#include "base/paths.h"
#include "base/strings.h"

namespace fs = std::filesystem;
namespace base = cutefish::base;
namespace strings = cutefish::base::strings;

static std::vector<std::string> desktopFiles(const std::string& directory)
{
    std::vector<std::string> names;
    std::error_code error;
    for (const auto& item : fs::directory_iterator(directory, error)) {
        const std::string name = item.path().filename().string();
        std::error_code typeError;
        if (name.ends_with(".desktop") && fs::is_regular_file(item.path(), typeError))
            names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

static bool isExecutableFile(const std::string& path)
{
    std::error_code error;
    return fs::is_regular_file(path, error) && ::access(path.c_str(), X_OK) == 0;
}

std::vector<std::string> autostartEntries(const std::vector<std::string>& directories,
                                          const std::vector<std::string>& desktops)
{
    std::vector<std::string> result;
    std::set<std::string> seen;
    for (const auto& directory : directories) {
        for (const auto& name : desktopFiles(directory)) {
            if (seen.contains(name))
                continue;
            // A disabled or invalid override must still mask lower-priority entries.
            seen.insert(name);
            const auto contents = base::file::read(directory + '/' + name);
            if (!contents)
                continue;
            std::istringstream stream(*contents);
            std::map<std::string, std::string> entry;
            bool desktopGroup = false;
            std::string rawLine;
            while (std::getline(stream, rawLine)) {
                const std::string line(strings::trimmed(rawLine));
                if (line.starts_with('[')) {
                    desktopGroup = line == "[Desktop Entry]";
                    continue;
                }
                if (!desktopGroup || line.starts_with('#'))
                    continue;
                const auto equals = line.find('=');
                if (equals != std::string::npos && equals > 0)
                    entry[std::string(strings::trimmed(line.substr(0, equals)))] =
                            std::string(strings::trimmed(line.substr(equals + 1)));
            }
            const auto value = [&entry](const std::string& key) {
                const auto found = entry.find(key);
                return found == entry.end() ? std::string() : found->second;
            };
            // The second key is how settings tools of other desktops switch an entry off.
            if (value("Hidden") == "true" || value("X-GNOME-Autostart-enabled") == "false"
                || value("Type") != "Application")
                continue;
            const auto matches = [&value, &desktops](const std::string& key) {
                const auto names = strings::split(value(key), ';', strings::SplitBehavior::SkipEmptyParts);
                for (const auto& desktop : desktops) {
                    if (std::find(names.begin(), names.end(), desktop) != names.end())
                        return true;
                }
                return false;
            };
            if ((entry.contains("OnlyShowIn") && !matches("OnlyShowIn")) || matches("NotShowIn"))
                continue;
            const auto executable = value("TryExec");
            if (!executable.empty()) {
                if (executable.starts_with('/')) {
                    if (!isExecutableFile(executable))
                        continue;
                } else if (executable.find('/') != std::string::npos
                           || base::paths::findExecutable(executable).empty()) {
                    continue;
                }
            }
            const auto command = value("Exec");
            if (!strings::trimmed(command).empty())
                result.push_back(command);
        }
    }
    return result;
}
