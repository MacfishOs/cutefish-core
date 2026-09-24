#include "gtksettings.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QVariant>

static QString gtk3SettingsIniPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QLatin1String("/gtk-3.0/settings.ini");
}

// The session runs GLib with GSETTINGS_BACKEND=keyfile, so org.gnome.desktop.interface lives in
// this plain-text file and GTK on Wayland (which ignores settings.ini for keys the schema covers)
// picks up changes through GLib's file monitor.
static QString gsettingsKeyfilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QLatin1String("/glib-2.0/settings/keyfile");
}

static QString gvariantString(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    escaped.replace(QLatin1Char('\''), QLatin1String("\\'"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

static QString gvariantValue(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::Int:
        return QString::number(value.toInt());
    default:
        return gvariantString(value.toString());
    }
}

// Rewrites one group of the keyfile and leaves every other line, including other applications'
// settings, untouched.
static void writeGSettingsGroup(const QString &group, const QMap<QString, QVariant> &values)
{
    const QString path = gsettingsKeyfilePath();
    QStringList lines;
    QFile in(path);
    if (in.open(QIODevice::ReadOnly | QIODevice::Text))
        lines = QString::fromUtf8(in.readAll()).split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    QMap<QString, QVariant> pending = values;
    const QString header = QLatin1Char('[') + group + QLatin1Char(']');
    int groupEnd = -1;
    bool inGroup = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            if (inGroup) {
                groupEnd = i;
                break;
            }
            inGroup = line == header;
            continue;
        }
        if (!inGroup)
            continue;

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        if (pending.contains(key)) {
            lines[i] = key + QLatin1Char('=') + gvariantValue(pending.take(key));
        }
    }
    if (inGroup && groupEnd < 0)
        groupEnd = lines.size();

    QStringList added;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        added << it.key() + QLatin1Char('=') + gvariantValue(it.value());

    if (!inGroup) {
        if (!lines.isEmpty())
            lines << QString();
        lines << header;
        lines << added;
    } else {
        // Keep the blank separator before the next group after the inserted keys.
        int insertAt = groupEnd;
        while (insertAt > 0 && lines.at(insertAt - 1).trimmed().isEmpty())
            --insertAt;
        for (int i = 0; i < added.size(); ++i)
            lines.insert(insertAt + i, added.at(i));
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    // GLib replaces the file atomically too; a partial write would be read as an empty keyfile.
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Cannot write" << path << out.errorString();
        return;
    }
    out.write(lines.join(QLatin1Char('\n')).toUtf8());
    out.write("\n");
    out.commit();
}

void writeGtkSettings(const GtkSettings &gtk)
{
    const QString fontName = QString("%1 %2").arg(gtk.fontFamily).arg(gtk.fontPointSize);
    // No Cutefish GTK theme ships; Adwaita's dark variant is what gtk-theme can switch to.
    const QString gtkTheme =
            gtk.darkMode ? QStringLiteral("Adwaita-dark") : QStringLiteral("Adwaita");

    // Only GTK clients on Xwayland still read this: without an XSETTINGS manager the X11 backend
    // has nothing else. On Wayland the keyfile below overrides every key the schema covers.
    QSettings settings(gtk3SettingsIniPath(), QSettings::IniFormat);
    settings.clear();
    settings.beginGroup("Settings");
    settings.setValue("gtk-font-name", fontName);
    settings.setValue("gtk-application-prefer-dark-theme", gtk.darkMode);
    settings.setValue("gtk-icon-theme-name", gtk.iconTheme);
    settings.setValue("gtk-cursor-theme-name", gtk.cursorTheme);
    settings.setValue("gtk-cursor-theme-size", gtk.cursorSize);
    settings.setValue("gtk-enable-animations", false);
    settings.setValue("gtk-theme-name", gtkTheme);
    settings.sync();

    writeGSettingsGroup(QStringLiteral("org/gnome/desktop/interface"), {
            {QStringLiteral("font-name"), fontName},
            {QStringLiteral("gtk-theme"), gtkTheme},
            {QStringLiteral("color-scheme"),
             gtk.darkMode ? QStringLiteral("prefer-dark") : QStringLiteral("default")},
            {QStringLiteral("icon-theme"), gtk.iconTheme},
            {QStringLiteral("cursor-theme"), gtk.cursorTheme},
            {QStringLiteral("cursor-size"), gtk.cursorSize},
            {QStringLiteral("enable-animations"), false},
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
