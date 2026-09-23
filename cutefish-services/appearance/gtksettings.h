#ifndef GTKSETTINGS_H
#define GTKSETTINGS_H

#include <QString>

struct GtkSettings
{
    QString fontFamily;
    qreal fontPointSize = 9;
    bool darkMode = false;
    QString iconTheme;
    QString cursorTheme;
    int cursorSize = 24;
};

// Mirrors the system appearance into GTK's config: settings.ini for Xwayland clients and the
// GSettings keyfile for Wayland clients.
void writeGtkSettings(const GtkSettings &settings);

#endif
