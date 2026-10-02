#include "fontconfigwriter.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QSaveFile>
#include <QStandardPaths>

void writeFontConfig(const FontConfig &config)
{
    const QStringList families = QFontDatabase::families();
    const QString familyFont = families.contains(config.family) ? config.family
            : families.contains(QStringLiteral("Noto Sans"))    ? QStringLiteral("Noto Sans")
                                                                : QStringLiteral("DejaVu Sans");
    const QString fixedFont = families.contains(config.fixedFamily) ? config.fixedFamily
            : families.contains(QStringLiteral("Noto Sans Mono"))
            ? QStringLiteral("Noto Sans Mono")
            : QStringLiteral("DejaVu Sans Mono");

    QFile templateFile(QStringLiteral(":/appearance/fontconfig.conf"));
    if (!templateFile.open(QIODevice::ReadOnly))
        return;

    const QString content = QString::fromUtf8(templateFile.readAll())
                                    .arg(familyFont.toHtmlEscaped(),
                                         fixedFont.toHtmlEscaped(),
                                         config.antialias ? QStringLiteral("true")
                                                          : QStringLiteral("false"),
                                         config.hintStyle.toHtmlEscaped(),
                                         config.rgba.toHtmlEscaped());

    const QString targetPath =
            QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QLatin1String("/fontconfig/conf.d");
    QDir().mkpath(targetPath);

    // Atomic replace: a fontconfig reader starting meanwhile must never parse a partial file.
    QSaveFile file(targetPath + QLatin1String("/99-cutefish.conf"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(content.toUtf8());
        file.commit();
    }
}

