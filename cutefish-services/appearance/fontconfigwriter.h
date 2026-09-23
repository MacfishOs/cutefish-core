#ifndef FONTCONFIGWRITER_H
#define FONTCONFIGWRITER_H

#include <QString>

struct FontConfig
{
    QString family;
    QString fixedFamily;
    bool antialias = true;
    QString hintStyle = QStringLiteral("hintslight");
};

// Writes ~/.config/fontconfig/conf.d/99-cutefish.conf, falling back to installed families.
void writeFontConfig(const FontConfig &config);

#endif
