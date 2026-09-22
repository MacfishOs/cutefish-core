/*
 * Copyright (C) 2021 CutefishOS Team.
 *
 * Author:     revenmartin <revenmartin@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "thememanager.h"
#include "appearanceadaptor.h"
#include "wallpapermanager.h"

#include <QDBusInterface>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

static const QByteArray s_systemFontName = QByteArrayLiteral("Font");
static const QByteArray s_systemFixedFontName = QByteArrayLiteral("FixedFont");
static const QByteArray s_systemPointFontSize = QByteArrayLiteral("FontSize");

static QString gtkRc2Path()
{
    return QDir::homePath() + QLatin1String("/.gtkrc-2.0");
}

static QString gtk3SettingsIniPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QLatin1String("/gtk-3.0/settings.ini");
}

ThemeManager *ThemeManager::self()
{
    static ThemeManager t;
    return &t;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
    , m_settings(new QSettings(QStringLiteral("cutefish"), QStringLiteral("theme")))
{
    if (!QFile::exists(m_settings->fileName())) {
        QFile file(m_settings->fileName());
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream in(&file);
            in << "";
            file.close();
        }
    }

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
    m_darkModeDimsWallpaer = m_settings->value("DarkModeDimsWallpaer", false).toBool();
    m_continuousCorners = m_settings->value("ContinuousCorners", true).toBool();
    m_accentColor = m_settings->value("AccentColor", 0).toInt();
    m_cursorTheme = m_settings->value("CursorTheme", "default").toString();
    m_cursorSize = m_settings->value("CursorSize", 24).toInt();
    m_iconTheme = m_settings->value("IconTheme", "Cutefish").toString();

    // Synchronize toolkit and compositor cursor settings at session startup.
    applyCursorSettings();

    // Init fonts.
    if (!m_settings->contains(s_systemFixedFontName)) {
        m_settings->setValue(s_systemFixedFontName, "Noto Mono");
    }

    if (!m_settings->contains(s_systemFontName)) {
        QSettings lanSettings(QStringLiteral("cutefish"), QStringLiteral("language"));
        QString languageCode = lanSettings.value("language").toString();
        QString fontName;

        if (languageCode == "zh_CN") {
            fontName = "Noto Sans CJK SC";
        } else if (languageCode.contains("en_")) {
            fontName = "Noto Sans";
        } else if (languageCode == "zh_TW") {
            fontName = "Noto Sans CJK TC";
        } else if (languageCode == "zh_HK") {
            fontName = "Noto Serif CJK HK";
        } else if (languageCode == "ja_JP") {
            fontName = "Noto Serif CJK JP";
        } else {
            fontName = "Noto Sans";
        }

        m_settings->setValue(s_systemFontName, fontName);
    }

    // 登陆后更新 fontconfig
    updateFontConfig();

    auto *iconThemeProcess = new QProcess(this);
    auto *deadline = new QTimer(iconThemeProcess);
    deadline->setSingleShot(true);
    connect(iconThemeProcess, &QProcess::finished, this,
            [iconThemeProcess, deadline](int exitCode, QProcess::ExitStatus status) {
                deadline->stop();
                if (status != QProcess::NormalExit || exitCode != 0)
                    qWarning() << "Cannot set GTK icon theme to Cutefish:"
                               << iconThemeProcess->readAllStandardError();
                iconThemeProcess->deleteLater();
            });
    connect(iconThemeProcess, &QProcess::errorOccurred, this,
            [iconThemeProcess, deadline](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    deadline->stop();
                    qWarning() << "Cannot start gsettings:" << iconThemeProcess->errorString();
                    iconThemeProcess->deleteLater();
                }
            });
    connect(deadline, &QTimer::timeout, iconThemeProcess, [iconThemeProcess] {
        qWarning() << "GTK icon theme update timed out";
        iconThemeProcess->kill();
    });
    iconThemeProcess->start(QStringLiteral("gsettings"),
                            {QStringLiteral("set"), QStringLiteral("org.gnome.desktop.interface"),
                             QStringLiteral("icon-theme"), QStringLiteral("Cutefish")});
    deadline->start(3000);
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

bool ThemeManager::darkModeDimsWallpaer() const
{
    return m_darkModeDimsWallpaer;
}

void ThemeManager::setDarkModeDimsWallpaer(bool value)
{
    if (m_darkModeDimsWallpaer == value)
        return;

    m_darkModeDimsWallpaer = value;
    m_settings->setValue("DarkModeDimsWallpaer", m_darkModeDimsWallpaer);

    emit darkModeDimsWallpaerChanged();
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
    return m_settings->value("AccentColor", 0).toInt();
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

void ThemeManager::updateGtk2Config()
{
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
    QSettings settings(gtk3SettingsIniPath(), QSettings::IniFormat);
    settings.clear();
    settings.beginGroup("Settings");

    // font
    settings.setValue("gtk-font-name",
                      QString("%1 %2").arg(systemFont()).arg(systemFontPointSize()));
    // dark mode
    settings.setValue("gtk-application-prefer-dark-theme", isDarkMode());
    // icon theme
    // "Cutefish" and the legacy Crule names follow dark mode.
    const bool iconFollowsDarkMode =
            m_iconTheme.isEmpty() || m_iconTheme == "Cutefish" || m_iconTheme.startsWith("Crule");
    settings.setValue("gtk-icon-theme-name",
                      iconFollowsDarkMode ? (isDarkMode() ? "Cutefish-Dark" : "Cutefish-Light")
                                          : m_iconTheme);
    settings.setValue("gtk-cursor-theme-name", m_cursorTheme);
    settings.setValue("gtk-cursor-theme-size", m_cursorSize);
    // other
    settings.setValue("gtk-enable-animations", true);
    // theme
    settings.setValue("gtk-theme-name", isDarkMode() ? "Cutefish-dark" : "Cutefish-light");
    settings.sync();
}

void ThemeManager::applyFontSettings()
{
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
    QFontDatabase database;
    const QStringList families = database.families();
    const QString familyFont = families.contains(systemFont()) ? systemFont()
            : families.contains(QStringLiteral("Noto Sans"))   ? QStringLiteral("Noto Sans")
                                                               : QStringLiteral("DejaVu Sans");
    const QString fixedFont = families.contains(systemFixedFont()) ? systemFixedFont()
            : families.contains(QStringLiteral("Noto Sans Mono"))
            ? QStringLiteral("Noto Sans Mono")
            : QStringLiteral("DejaVu Sans Mono");
    const QString familyFallback = familyFont;

    QSettings settings(QSettings::UserScope, "cutefish", "theme");
    bool hinting = settings.value("FontAntialias", true).toBool();
    QString hintStyle = settings.value("FontHintStyle", "hintslight").toString();

    QString content = QString("<?xml version=\"1.0\"?>"
                              "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">"
                              "<fontconfig>"
                              "<match target=\"pattern\">"
                              "<test qual=\"any\" name=\"family\">"
                              "<string>serif</string>"
                              "</test>"
                              "<edit name=\"family\" mode=\"assign\" binding=\"strong\">"
                              "<string>%1</string>"
                              "<string>%2</string>"
                              "</edit>"
                              "</match>"
                              "<match target=\"pattern\">"
                              "<test qual=\"any\" name=\"family\">"
                              "<string>sans-serif</string>"
                              "</test>"
                              "<edit name=\"family\" mode=\"assign\" binding=\"strong\">"
                              "<string>%3</string>"
                              "<string>%4</string>"
                              "</edit>"
                              "</match>"
                              "<match target=\"pattern\">"
                              "<test qual=\"any\" name=\"family\">"
                              "<string>monospace</string>"
                              "</test>"
                              "<edit name=\"family\" mode=\"assign\" binding=\"strong\">"
                              "<string>%5</string>"
                              "<string>%6</string>"
                              "<string>%7</string>"
                              "</edit>"
                              "</match>"
                              "<match target=\"font\">"
                              "<edit name=\"rgba\" mode=\"assign\"><const>rgb</const></edit>"
                              "</match>"
                              "<match target=\"font\">"
                              "<edit name=\"hinting\" mode=\"assign\">"
                              "<bool>%8</bool>"
                              "</edit>"
                              "</match>"
                              "<match target=\"font\">"
                              "<edit name=\"hintstyle\" mode=\"assign\">"
                              "<const>%9</const>"
                              "</edit>"
                              "</match>"
                              "</fontconfig>")
                              .arg(familyFont)
                              .arg(familyFallback)
                              .arg(familyFont)
                              .arg(familyFallback)
                              .arg(fixedFont)
                              .arg(fixedFont)
                              .arg(familyFont)
                              .arg(hinting ? "true" : "false")
                              .arg(hintStyle);

    QString targetPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                       + QLatin1Char('/') + QLatin1String("fontconfig"));

    if (!QDir(targetPath).exists()) {
        QDir(targetPath).mkpath(targetPath);
    }

    targetPath += "/conf.d";

    if (!QDir(targetPath).exists()) {
        QDir(targetPath).mkpath(targetPath);
    }

    QString xmlOut;
    QXmlStreamReader reader(content);
    QXmlStreamWriter writer(&xmlOut);
    writer.setAutoFormatting(true);

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isWhitespace()) {
            writer.writeCurrentToken(reader);
        }
    }

    QFile file(targetPath + "/99-cutefish.conf");
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream s(&file);
        s << xmlOut.toLatin1();
        file.close();
    }
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
    updateGtk3Config();
    emit iconThemeChanged();
}
