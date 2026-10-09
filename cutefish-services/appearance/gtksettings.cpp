#include "gtksettings.h"
#include "keyfile.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

static QString gtk3SettingsIniPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QLatin1String("/gtk-3.0/settings.ini");
}

void writeGtkSettings(const GtkSettings &gtk)
{
    const QString fontName = QString("%1 %2").arg(gtk.fontFamily).arg(gtk.fontPointSize);
    // No Cutefish GTK theme ships; Adwaita's dark variant is what gtk-theme can switch to.
    const QString gtkTheme =
            gtk.darkMode ? QStringLiteral("Adwaita-dark") : QStringLiteral("Adwaita");
    const bool subpixel = gtk.fontAntialias && gtk.fontRgba != QLatin1String("none");

    // Only GTK clients on Xwayland still read this, for every key the compositor's XSETTINGS
    // leaves out. On Wayland the keyfile below overrides every key the schema covers.
    QSettings settings(gtk3SettingsIniPath(), QSettings::IniFormat);
    settings.clear();
    settings.beginGroup("Settings");
    settings.setValue("gtk-font-name", fontName);
    settings.setValue("gtk-xft-antialias", gtk.fontAntialias ? 1 : 0);
    settings.setValue("gtk-xft-hinting", gtk.fontHintStyle != QLatin1String("hintnone") ? 1 : 0);
    settings.setValue("gtk-xft-hintstyle", gtk.fontHintStyle);
    settings.setValue("gtk-xft-rgba", subpixel ? gtk.fontRgba : QStringLiteral("none"));
    settings.setValue("gtk-application-prefer-dark-theme", gtk.darkMode);
    settings.setValue("gtk-icon-theme-name", gtk.iconTheme);
    settings.setValue("gtk-cursor-theme-name", gtk.cursorTheme);
    settings.setValue("gtk-cursor-theme-size", gtk.cursorSize);
    settings.setValue("gtk-enable-animations", false);
    settings.setValue("gtk-theme-name", gtkTheme);
    settings.sync();

    // GTK on Wayland ignores settings.ini for keys the schema covers and picks up keyfile
    // changes through GLib's file monitor.
    GSettingsKeyfile::writeGroup(QStringLiteral("org/gnome/desktop/interface"), {
            {QStringLiteral("font-name"), fontName},
            {QStringLiteral("font-antialiasing"),
             !gtk.fontAntialias ? QStringLiteral("none")
                     : subpixel ? QStringLiteral("rgba")
                                : QStringLiteral("grayscale")},
            // The schema's nicks are fontconfig's hintstyle constants without "hint".
            {QStringLiteral("font-hinting"), gtk.fontHintStyle.mid(4)},
            {QStringLiteral("font-rgba-order"), subpixel ? gtk.fontRgba : QStringLiteral("rgb")},
            // "automatic" makes GTK 4 ignore the three keys above.
            {QStringLiteral("font-rendering"), QStringLiteral("manual")},
            {QStringLiteral("gtk-theme"), gtkTheme},
            {QStringLiteral("color-scheme"),
             gtk.darkMode ? QStringLiteral("prefer-dark") : QStringLiteral("default")},
            {QStringLiteral("icon-theme"), gtk.iconTheme},
            {QStringLiteral("cursor-theme"), gtk.cursorTheme},
            {QStringLiteral("cursor-size"), gtk.cursorSize},
            {QStringLiteral("enable-animations"), false},
    });
    GSettingsKeyfile::writeGroup(QStringLiteral("org/gnome/desktop/wm/preferences"), {
            {QStringLiteral("button-layout"), QStringLiteral("menu:minimize,maximize,close")},
    });
}

static void ensureGtkCssImport(const QString &gtkDir)
{
    const QString importLine = QStringLiteral("@import url(\"%1\");")
            .arg(QUrl::fromLocalFile(QStringLiteral(CUTEFISH_GTK_CSS_DIR "/%1.css").arg(gtkDir))
                         .toString());

    QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QLatin1Char('/') + gtkDir + QLatin1String("/gtk.css");
    // Write through a symlinked gtk.css (dotfile managers) instead of replacing the link.
    const QFileInfo info(path);
    if (info.isSymLink() && info.exists())
        path = info.canonicalFilePath();

    QStringList lines;
    QFile in(path);
    if (in.open(QIODevice::ReadOnly | QIODevice::Text))
        lines = QString::fromUtf8(in.readAll()).split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    // GTK drops an @import that follows any rule, so ours has to lead the file. Rewriting an
    // unchanged file would make every running GTK client reload its styles.
    if (!lines.isEmpty() && lines.first() == importLine)
        return;
    lines.removeAll(importLine);
    lines.prepend(importLine);

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Cannot write" << path << out.errorString();
        return;
    }
    out.write(lines.join(QLatin1Char('\n')).toUtf8());
    out.write("\n");
    out.commit();
}

void ensureGtkCssImports()
{
    ensureGtkCssImport(QStringLiteral("gtk-3.0"));
    ensureGtkCssImport(QStringLiteral("gtk-4.0"));
}
