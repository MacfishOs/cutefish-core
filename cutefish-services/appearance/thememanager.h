// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include "fontconfigwriter.h"

#include <QObject>
#include <QSettings>
#include <QStringList>

class WallpaperManager;

class ThemeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isDarkMode READ isDarkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(bool backgroundVisible READ backgroundVisible WRITE setBackgroundVisible NOTIFY
                       backgroundVisibleChanged)
    Q_PROPERTY(bool continuousCorners READ continuousCorners WRITE setContinuousCorners NOTIFY
                       continuousCornersChanged)
    Q_PROPERTY(QString systemFont READ systemFont WRITE setSystemFont NOTIFY systemFontChanged)
    Q_PROPERTY(QString systemFixedFont READ systemFixedFont WRITE setSystemFixedFont)
    Q_PROPERTY(qreal systemFontPointSize READ systemFontPointSize WRITE setSystemFontPointSize
                       NOTIFY systemFontPointSizeChanged)
    Q_PROPERTY(QString wallpaper READ wallpaper WRITE setWallpaper NOTIFY wallpaperChanged)
    Q_PROPERTY(QStringList userWallpapers READ userWallpapers NOTIFY userWallpapersChanged)
    Q_PROPERTY(QStringList wallpaperFolders READ wallpaperFolders NOTIFY wallpaperFoldersChanged)
    Q_PROPERTY(int accentColor READ accentColor WRITE setAccentColor NOTIFY accentColorChanged)
    Q_PROPERTY(int backgroundType READ backgroundType WRITE setBackgroundType NOTIFY
                       backgroundTypeChanged)
    Q_PROPERTY(QString backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY
                       backgroundColorChanged)
    Q_PROPERTY(QString cursorTheme READ cursorTheme WRITE setCursorTheme NOTIFY cursorThemeChanged)
    Q_PROPERTY(int cursorSize READ cursorSize WRITE setCursorSize NOTIFY cursorSizeChanged)
    Q_PROPERTY(QString iconTheme READ iconTheme WRITE setIconTheme NOTIFY iconThemeChanged)

public:
    static ThemeManager *self();
    // The UI font a language starts with; empty when it has none of its own.
    static QString fontForLocale(const QString &localeName);

    ThemeManager(QObject *parent = nullptr);

    bool isDarkMode();
    void setDarkMode(bool darkMode);

    bool continuousCorners() const;
    void setContinuousCorners(bool value);

    QString systemFont();
    bool backgroundVisible() const;
    void setBackgroundVisible(bool value);
    void setSystemFont(const QString &fontFamily);

    QString systemFixedFont();
    void setSystemFixedFont(const QString &fontFamily);

    qreal systemFontPointSize();
    void setSystemFontPointSize(qreal fontSize);

    QString wallpaper();
    void setWallpaper(const QString &path);
    QStringList userWallpapers() const;
    void removeUserWallpaper(const QString &path);

    QStringList wallpaperFolders() const;
    void addWallpaperFolder(const QString &path);
    void removeWallpaperFolder(const QString &path);
    QStringList wallpaperFolderImages(const QString &path);

    int backgroundType();
    void setBackgroundType(int type);

    QString backgroundColor();
    void setBackgroundColor(QString color);

    int accentColor();
    void setAccentColor(int accentColor);

    QString cursorTheme() const;
    void setCursorTheme(const QString &theme);

    int cursorSize() const;
    void setCursorSize(int size);

    void updateGtk3Config();
    void setFontRendering(bool antialias, const QString &hintStyle);
    void applyCursorSettings();

    QString iconTheme() const;
    void setIconTheme(const QString &iconTheme);

    void updateFontConfig();

signals:
    void darkModeChanged(bool darkMode);
    void wallpaperChanged(QString path);
    void userWallpapersChanged();
    void wallpaperFoldersChanged();
    void wallpaperFolderImagesChanged(QString folder);
    void continuousCornersChanged(bool continuousCorners);
    void accentColorChanged(int accentColor);
    void backgroundVisibleChanged();
    void backgroundTypeChanged();
    void backgroundColorChanged();
    void cursorThemeChanged();
    void cursorSizeChanged();
    void iconThemeChanged();
    void systemFontPointSizeChanged();
    void systemFontChanged();

private:
    FontConfig fontConfig();

    QSettings *m_settings;
    WallpaperManager *m_wallpaperManager;

    bool m_isDarkMode;
    bool m_continuousCorners;
    int m_accentColor;

    QString m_cursorTheme;
    int m_cursorSize;

    QString m_iconTheme;
};

#endif
