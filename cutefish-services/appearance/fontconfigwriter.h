#ifndef FONTCONFIGWRITER_H
#define FONTCONFIGWRITER_H

#include <QString>

struct FontConfig
{
    QString family;
    QString fixedFamily;
    bool antialias = true;
    QString hintStyle = QStringLiteral("hintfull");
    QString rgba = QStringLiteral("rgb");
    // Last resort for Han, kana and hangul the families above lack.
    QString cjkFamily = QStringLiteral("Noto Sans CJK SC");
};

// Writes ~/.config/fontconfig/conf.d/99-cutefish.conf, falling back to installed families.
void writeFontConfig(const FontConfig &config);

#endif
