#include "fontconfigwriter.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QStandardPaths>
#include <QTextStream>

void writeFontConfig(const FontConfig &config)
{
    QFontDatabase database;
    const QStringList families = database.families();
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

    QString targetPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                       + QLatin1Char('/') + QLatin1String("fontconfig"));

    if (!QDir(targetPath).exists()) {
        QDir(targetPath).mkpath(targetPath);
    }

    targetPath += "/conf.d";

    if (!QDir(targetPath).exists()) {
        QDir(targetPath).mkpath(targetPath);
    }

    QFile file(targetPath + "/99-cutefish.conf");
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream s(&file);
        s << content;
        file.close();
    }
}

