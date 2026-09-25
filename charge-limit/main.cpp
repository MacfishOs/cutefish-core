#include <QCoreApplication>
#include <QDir>
#include <QFile>

// Sets the charge limit of every battery whose driver has one, through the
// kernel's charge_control_*_threshold attributes. Settings runs it via pkexec:
//   cutefish-charge-limit <80|85|90|95|100>

static const QString s_powerSupplyPath = QStringLiteral("/sys/class/power_supply/");

static int readAttribute(const QString &path)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly))
        return -1;

    return file.readAll().trimmed().toInt();
}

static bool writeAttribute(const QString &path, int value)
{
    QFile file(path);

    // Unbuffered, so a value the driver rejects fails the write itself.
    if (!file.open(QIODevice::WriteOnly | QIODevice::Unbuffered))
        return false;

    return file.write(QByteArray::number(value)) > 0;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QStringList arguments = app.arguments();
    bool ok = false;
    const int end = arguments.size() == 2 ? arguments.at(1).toInt(&ok) : 0;

    // Only the steps Settings offers; 100 lifts the limit.
    if (!ok || end < 80 || end > 100 || end % 5 != 0)
        return 2;

    // Charging resumes 5% below the limit rather than after every percent it
    // drops; a start of 0 is the drivers' "no limit".
    const int start = end >= 100 ? 0 : end - 5;
    int applied = 0;

    const QStringList supplies = QDir(s_powerSupplyPath).entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &supply : supplies) {
        const QString path = s_powerSupplyPath + supply + QLatin1Char('/');
        const QString endPath = path + QStringLiteral("charge_control_end_threshold");
        const QString startPath = path + QStringLiteral("charge_control_start_threshold");

        QFile type(path + QStringLiteral("type"));

        if (!type.open(QIODevice::ReadOnly) || type.readAll().trimmed() != "Battery" || !QFile::exists(endPath))
            continue;

        const bool hasStart = QFile::exists(startPath);

        // Drivers reject an end below the start, so lower a start in the way first.
        if (hasStart && readAttribute(startPath) >= end)
            writeAttribute(startPath, start);

        if (!writeAttribute(endPath, end))
            return 1;

        // Best effort: some drivers bound the start, e.g. to 50 and up.
        if (hasStart)
            writeAttribute(startPath, start);

        ++applied;
    }

    return applied > 0 ? 0 : 1;
}
