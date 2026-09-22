#ifndef WALLPAPERMANAGER_H
#define WALLPAPERMANAGER_H

#include <QObject>
#include <QSet>
#include <QStringList>

class QSettings;

/**
 * The desktop background: the wallpaper in use, the wallpapers the user
 * brought in, and the folders they added as libraries. ThemeManager owns the
 * D-Bus interface and hands this part of it over here.
 *
 * Every wallpaper the user picks is copied into the session's own data
 * directory, so moving or deleting the original leaves the desktop alone.
 */
class WallpaperManager : public QObject
{
    Q_OBJECT

public:
    explicit WallpaperManager(QSettings *settings, QObject *parent = nullptr);

    QString wallpaper() const;
    void setWallpaper(const QString &path);

    QStringList userWallpapers() const;
    void removeUserWallpaper(const QString &path);

    QStringList wallpaperFolders() const;
    void addWallpaperFolder(const QString &path);
    void removeWallpaperFolder(const QString &path);
    QStringList wallpaperFolderImages(const QString &path);

    int backgroundType() const;
    void setBackgroundType(int type);

    QString backgroundColor() const;
    void setBackgroundColor(const QString &color);

    bool backgroundVisible() const;
    void setBackgroundVisible(bool visible);

signals:
    void wallpaperChanged(QString path);
    void userWallpapersChanged();
    void wallpaperFoldersChanged();
    void wallpaperFolderImagesChanged(QString folder);
    void backgroundTypeChanged();
    void backgroundColorChanged();
    void backgroundVisibleChanged();

private:
    void syncWallpaperFolder(const QString &folder);
    void onWallpaperFolderSynced(const QString &folder, bool copied);

    QSettings *m_settings;

    QString m_wallpaperPath;
    QStringList m_userWallpapers;
    QStringList m_wallpaperFolders;
    QSet<QString> m_syncingFolders;

    bool m_backgroundVisible;
    int m_backgroundType;
    QString m_backgroundColor;
};

#endif // WALLPAPERMANAGER_H
