#ifndef WALLPAPERMANAGER_H
#define WALLPAPERMANAGER_H

#include <map>
#include <string>
#include <vector>

#include "base/settings.h"
#include "base/signal.h"
#include "base/thread_pool.h"

/**
 * The desktop background: the wallpaper in use, the wallpapers the user
 * brought in, and the folders they added as libraries. ThemeManager owns the
 * D-Bus interface and hands this part of it over here.
 *
 * Every wallpaper the user picks is copied into the session's own data
 * directory, so moving or deleting the original leaves the desktop alone.
 */
class WallpaperManager
{
public:
    explicit WallpaperManager(cutefish::base::Settings &settings);
    ~WallpaperManager();

    WallpaperManager(const WallpaperManager &) = delete;
    WallpaperManager &operator=(const WallpaperManager &) = delete;

    std::string wallpaper() const;
    void setWallpaper(const std::string &path);

    std::vector<std::string> userWallpapers() const;
    void removeUserWallpaper(const std::string &path);

    std::vector<std::string> wallpaperFolders() const;
    void addWallpaperFolder(const std::string &path);
    void removeWallpaperFolder(const std::string &path);
    std::vector<std::string> wallpaperFolderImages(const std::string &path);

    int backgroundType() const;
    void setBackgroundType(int type);

    std::string backgroundColor() const;
    void setBackgroundColor(const std::string &color);

    bool backgroundVisible() const;
    void setBackgroundVisible(bool visible);

    cutefish::base::Signal<std::string> wallpaperChanged;
    cutefish::base::Signal<> userWallpapersChanged;
    cutefish::base::Signal<> wallpaperFoldersChanged;
    cutefish::base::Signal<std::string> wallpaperFolderImagesChanged;
    cutefish::base::Signal<> backgroundTypeChanged;
    cutefish::base::Signal<> backgroundColorChanged;
    cutefish::base::Signal<> backgroundVisibleChanged;

private:
    template<typename T>
    void write(const std::string &key, const T &value);
    void syncWallpaperFolder(const std::string &folder);
    void onWallpaperFolderSynced(const std::string &folder, bool copied);

    cutefish::base::Settings &m_settings;

    std::string m_wallpaperPath;
    std::vector<std::string> m_userWallpapers;
    std::vector<std::string> m_wallpaperFolders;
    // The copies being made, by folder.
    std::map<std::string, cutefish::base::Job> m_syncingFolders;

    bool m_backgroundVisible = true;
    int m_backgroundType = 0;
    std::string m_backgroundColor;
};

#endif // WALLPAPERMANAGER_H
