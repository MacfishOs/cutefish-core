#include "fontconfigwriter.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QStandardPaths>
#include <QTextStream>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

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
    const QString familyFallback = familyFont;

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
                              .arg(config.antialias ? "true" : "false")
                              .arg(config.hintStyle);

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

