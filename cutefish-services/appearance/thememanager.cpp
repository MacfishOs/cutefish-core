// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thememanager.h"
#include "appearanceadaptor.h"
#include "fontconfigwriter.h"
#include "gtksettings.h"
#include "languageenvironment.h"
#include "preferences.h"
#include "wallpapermanager.h"

#include <QDBusConnection>

static const QByteArray s_systemFontName = QByteArrayLiteral("Font");
static const QByteArray s_systemFixedFontName = QByteArrayLiteral("FixedFont");
static const QByteArray s_systemPointFontSize = QByteArrayLiteral("FontSize");

ThemeManager *ThemeManager::self()
{
    static ThemeManager t;
    return &t;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
    , m_settings(new QSettings(Preferences::file(QStringLiteral("services")), QSettings::IniFormat, this))
{
    m_settings->beginGroup(QStringLiteral("Appearance"));

    m_wallpaperManager = new WallpaperManager(m_settings, this);
    connect(m_wallpaperManager, &WallpaperManager::wallpaperChanged, this,
            &ThemeManager::wallpaperChanged);
    connect(m_wallpaperManager, &WallpaperManager::userWallpapersChanged, this,
            &ThemeManager::userWallpapersChanged);
    connect(m_wallpaperManager, &WallpaperManager::wallpaperFoldersChanged, this,
            &ThemeManager::wallpaperFoldersChanged);
    connect(m_wallpaperManager, &WallpaperManager::wallpaperFolderImagesChanged, this,
            &ThemeManager::wallpaperFolderImagesChanged);
    connect(m_wallpaperManager, &WallpaperManager::backgroundTypeChanged, this,
            &ThemeManager::backgroundTypeChanged);
    connect(m_wallpaperManager, &WallpaperManager::backgroundColorChanged, this,
            &ThemeManager::backgroundColorChanged);
    connect(m_wallpaperManager, &WallpaperManager::backgroundVisibleChanged, this,
            &ThemeManager::backgroundVisibleChanged);

    // init dbus
    new AppearanceAdaptor(this);
    QDBusConnection::sessionBus().registerObject(
            QStringLiteral("/com/cutefish/Services/Appearance"), this);

    // init value
    m_isDarkMode = m_settings->value("DarkMode", false).toBool();
    m_continuousCorners = m_settings->value("ContinuousCorners", true).toBool();
    m_accentColor = m_settings->value("AccentColor", 0).toInt();
    m_cursorTheme = m_settings->value("CursorTheme", "default").toString();
    m_cursorSize = m_settings->value("CursorSize", 24).toInt();
    m_iconTheme = m_settings->value("IconTheme", "Cutefish").toString();

    // Init fonts.
    if (!m_settings->contains(s_systemFixedFontName)) {
        m_settings->setValue(s_systemFixedFontName, "Noto Mono");
    }

    if (!m_settings->contains(s_systemFontName)) {
        const QString fontName = fontForLocale(LanguageEnvironment::preferredLanguages().first());
        m_settings->setValue(s_systemFontName, fontName.isEmpty() ? QStringLiteral("Noto Sans") : fontName);
    }

    // Update fontconfig after login.
    updateFontConfig();

    // GTK on Wayland takes these from gsettings, not settings.ini; sync them at login.
    updateGtk3Config();
    ensureGtkCssImports();
}

bool ThemeManager::isDarkMode()
{
    return m_isDarkMode;
}

void ThemeManager::setDarkMode(bool darkMode)
{
    if (darkMode == m_isDarkMode)
        return;

    m_isDarkMode = darkMode;
    m_settings->setValue("DarkMode", darkMode);

    updateGtk3Config();

    emit darkModeChanged(m_isDarkMode);
}

bool ThemeManager::continuousCorners() const
{
    return m_continuousCorners;
}

void ThemeManager::setContinuousCorners(bool value)
{
    if (m_continuousCorners == value)
        return;

    m_continuousCorners = value;
    m_settings->setValue("ContinuousCorners", m_continuousCorners);

    emit continuousCornersChanged(m_continuousCorners);
}

bool ThemeManager::backgroundVisible() const
{
    return m_wallpaperManager->backgroundVisible();
}

void ThemeManager::setBackgroundVisible(bool value)
{
    m_wallpaperManager->setBackgroundVisible(value);
}

int ThemeManager::accentColor()
{
    return m_accentColor;
}

void ThemeManager::setAccentColor(int accentColor)
{
    if (m_accentColor == accentColor)
        return;

    m_accentColor = accentColor;
    m_settings->setValue("AccentColor", m_accentColor);

    emit accentColorChanged(m_accentColor);
}

QString ThemeManager::cursorTheme() const
{
    return m_cursorTheme;
}

void ThemeManager::setCursorTheme(const QString &theme)
{
    if (theme.isEmpty() || theme.size() > 255 || theme == "." || theme == ".."
        || theme.contains('/') || theme.contains('\\') || theme.contains(QChar::Null))
        return;
    if (m_cursorTheme != theme) {
        m_cursorTheme = theme;
        m_settings->setValue("CursorTheme", m_cursorTheme);
        applyCursorSettings();
        emit cursorThemeChanged();
    }
}

int ThemeManager::cursorSize() const
{
    return m_cursorSize;
}

void ThemeManager::setCursorSize(int size)
{
    size = qBound(16, size, 64);
    if (m_cursorSize != size) {
        m_cursorSize = size;
        m_settings->setValue("CursorSize", m_cursorSize);
        applyCursorSettings();
        emit cursorSizeChanged();
    }
}

QString ThemeManager::fontForLocale(const QString &localeName)
{
    if (localeName == "zh_CN")
        return QStringLiteral("Noto Sans CJK SC");
    if (localeName.startsWith("en_"))
        return QStringLiteral("Noto Sans");
    if (localeName == "zh_TW")
        return QStringLiteral("Noto Sans CJK TC");
    if (localeName == "zh_HK")
        return QStringLiteral("Noto Sans CJK HK");
    if (localeName == "ja_JP")
        return QStringLiteral("Noto Sans CJK JP");
    return {};
}

QString ThemeManager::systemFont()
{
    return m_settings->value(s_systemFontName, "Noto Sans").toString();
}

void ThemeManager::setSystemFont(const QString &fontFamily)
{
    m_settings->setValue(s_systemFontName, fontFamily);
    updateGtk3Config();
    updateFontConfig();

    emit systemFontChanged();
}

QString ThemeManager::systemFixedFont()
{
    return m_settings->value(s_systemFixedFontName, "Noto Mono").toString();
}

void ThemeManager::setSystemFixedFont(const QString &fontFamily)
{
    m_settings->setValue(s_systemFixedFontName, fontFamily);

    updateFontConfig();
}

qreal ThemeManager::systemFontPointSize()
{
    return m_settings->value(s_systemPointFontSize, 9).toReal();
}

void ThemeManager::setSystemFontPointSize(qreal fontSize)
{
    m_settings->setValue(s_systemPointFontSize, fontSize);
    updateGtk3Config();
    emit systemFontPointSizeChanged();
}

QString ThemeManager::wallpaper()
{
    return m_wallpaperManager->wallpaper();
}

void ThemeManager::setWallpaper(const QString &path)
{
    m_wallpaperManager->setWallpaper(path);
}

QStringList ThemeManager::userWallpapers() const
{
    return m_wallpaperManager->userWallpapers();
}

void ThemeManager::removeUserWallpaper(const QString &path)
{
    m_wallpaperManager->removeUserWallpaper(path);
}

QStringList ThemeManager::wallpaperFolders() const
{
    return m_wallpaperManager->wallpaperFolders();
}

void ThemeManager::addWallpaperFolder(const QString &path)
{
    m_wallpaperManager->addWallpaperFolder(path);
}

void ThemeManager::removeWallpaperFolder(const QString &path)
{
    m_wallpaperManager->removeWallpaperFolder(path);
}

QStringList ThemeManager::wallpaperFolderImages(const QString &path)
{
    return m_wallpaperManager->wallpaperFolderImages(path);
}

int ThemeManager::backgroundType()
{
    return m_wallpaperManager->backgroundType();
}

void ThemeManager::setBackgroundType(int type)
{
    m_wallpaperManager->setBackgroundType(type);
}

QString ThemeManager::backgroundColor()
{
    return m_wallpaperManager->backgroundColor();
}

void ThemeManager::setBackgroundColor(QString color)
{
    m_wallpaperManager->setBackgroundColor(color);
}

void ThemeManager::updateGtk3Config()
{
    GtkSettings gtk;
    gtk.fontFamily = systemFont();
    gtk.fontPointSize = systemFontPointSize();

    const FontConfig font = fontConfig();
    gtk.fontAntialias = font.antialias;
    gtk.fontHintStyle = font.hintStyle;
    gtk.fontRgba = font.rgba;

    gtk.darkMode = isDarkMode();
    gtk.iconTheme = m_iconTheme.isEmpty() ? QStringLiteral("Cutefish") : m_iconTheme;
    gtk.cursorTheme = m_cursorTheme;
    gtk.cursorSize = m_cursorSize;
    writeGtkSettings(gtk);
}

void ThemeManager::setFontRendering(bool antialias, const QString &hintStyle)
{
    m_settings->setValue("FontAntialias", antialias);
    m_settings->setValue("FontHintStyle", hintStyle);
    m_settings->sync();
    updateFontConfig();
    updateGtk3Config();
}

void ThemeManager::applyCursorSettings()
{
    m_settings->sync();

    updateGtk3Config();
}

void ThemeManager::updateFontConfig()
{
    writeFontConfig(fontConfig());
}

FontConfig ThemeManager::fontConfig()
{
    FontConfig config;
    config.family = systemFont();
    config.fixedFamily = systemFixedFont();
    config.antialias = m_settings->value("FontAntialias", true).toBool();
    config.hintStyle = m_settings->value("FontHintStyle", "hintslight").toString();
    return config;
}

QString ThemeManager::iconTheme() const
{
    return m_iconTheme;
}

void ThemeManager::setIconTheme(const QString &iconTheme)
{
    if (m_iconTheme == iconTheme)
        return;

    m_iconTheme = iconTheme;
    m_settings->setValue("IconTheme", m_iconTheme);
    // Listeners re-read the file when the signal arrives.
    m_settings->sync();
    updateGtk3Config();
    emit iconThemeChanged();
}
