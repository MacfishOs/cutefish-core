// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nightlightschedule.h"

#include <QFile>

#include <cmath>

namespace NightLightSchedule {

namespace {

constexpr double kPi = 3.14159265358979323846;

double radians(double degrees)
{
    return degrees * kPi / 180.0;
}

// One ISO 6709 angle of zone.tab: a sign, the degrees in `degreeDigits`
// digits, then minutes and optionally seconds.
std::optional<double> parseAngle(const QByteArray &text, int degreeDigits)
{
    if (text.isEmpty() || (text[0] != '+' && text[0] != '-'))
        return std::nullopt;
    const QByteArray digits = text.mid(1);
    if (digits.size() != degreeDigits + 2 && digits.size() != degreeDigits + 4)
        return std::nullopt;
    bool degreesOk = false, minutesOk = false, secondsOk = true;
    const int degrees = digits.left(degreeDigits).toInt(&degreesOk);
    const int minutes = digits.mid(degreeDigits, 2).toInt(&minutesOk);
    const int seconds =
            digits.size() > degreeDigits + 2 ? digits.mid(degreeDigits + 2).toInt(&secondsOk) : 0;
    if (!degreesOk || !minutesOk || !secondsOk)
        return std::nullopt;
    const double value = degrees + minutes / 60.0 + seconds / 3600.0;
    return text[0] == '-' ? -value : value;
}

} // namespace

Position position(const QDateTime &now, const TimesForDate &times)
{
    const QDate today = now.date();
    const Times day = times(today);
    if (day.start == day.end)
        return {false, {}};

    const auto at = [&now](QDate date, QTime time) {
        const QDateTime turn(date, time, now.timeZone());
        // When the clocks go back, the first occurrence of a repeated time may have passed.
        return turn > now ? turn
                          : QDateTime(date, time, now.timeZone(),
                                      QDateTime::TransitionResolution::PreferAfter);
    };
    const QTime time = now.time();
    const QDate tomorrow = today.addDays(1);
    if (day.start < day.end) {
        if (time < day.start)
            return {false, at(today, day.start)};
        if (time < day.end)
            return {true, at(today, day.end)};
        return {false, at(tomorrow, times(tomorrow).start)};
    }
    if (time < day.end)
        return {true, at(today, day.end)};
    if (time < day.start)
        return {false, at(today, day.start)};
    return {true, at(tomorrow, times(tomorrow).end)};
}

std::optional<SunTimes> sunTimes(QDate date, double latitude, double longitude,
                                 const QTimeZone &zone)
{
    // The fractional year, the equation of time in minutes and the solar
    // declination, from NOAA's General Solar Position Calculations.
    const double gamma = 2 * kPi / date.daysInYear() * (date.dayOfYear() - 1);
    const double equationOfTime = 229.18
            * (0.000075 + 0.001868 * std::cos(gamma) - 0.032077 * std::sin(gamma)
               - 0.014615 * std::cos(2 * gamma) - 0.040849 * std::sin(2 * gamma));
    const double declination = 0.006918 - 0.399912 * std::cos(gamma) + 0.070257 * std::sin(gamma)
            - 0.006758 * std::cos(2 * gamma) + 0.000907 * std::sin(2 * gamma)
            - 0.002697 * std::cos(3 * gamma) + 0.00148 * std::sin(3 * gamma);

    // The sun's centre 0.833° below the horizon allows for refraction and its radius.
    const double phi = radians(latitude);
    const double cosHourAngle = std::cos(radians(90.833)) / (std::cos(phi) * std::cos(declination))
            - std::tan(phi) * std::tan(declination);
    if (!(cosHourAngle >= -1 && cosHourAngle <= 1))
        return std::nullopt;
    const double hourAngle = std::acos(cosHourAngle) * 180.0 / kPi;

    // Minutes from UTC midnight, which may fall on the day before or after.
    const QDateTime midnight(date, QTime(0, 0), QTimeZone::UTC);
    const auto at = [&](double minutes) {
        return midnight.addSecs(qint64(std::lround(minutes * 60))).toTimeZone(zone);
    };
    return SunTimes {at(720 - 4 * (longitude + hourAngle) - equationOfTime),
                     at(720 - 4 * (longitude - hourAngle) - equationOfTime)};
}

QStringList zoneTables()
{
    return {QStringLiteral("/usr/share/zoneinfo/zone1970.tab"),
            QStringLiteral("/usr/share/zoneinfo/zone.tab")};
}

std::optional<Coordinates> zoneCoordinates(const QByteArray &zoneId, const QStringList &tables)
{
    if (zoneId.isEmpty())
        return std::nullopt;
    for (const QString &path : tables) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        while (!file.atEnd()) {
            const QByteArray line = file.readLine().trimmed();
            if (line.isEmpty() || line.startsWith('#'))
                continue;
            // Country codes, coordinates, zone, comment.
            const QList<QByteArray> fields = line.split('\t');
            if (fields.size() < 3 || fields[2] != zoneId)
                continue;
            const QByteArray &iso = fields[1];
            int split = 1;
            while (split < iso.size() && iso[split] != '+' && iso[split] != '-')
                ++split;
            const auto latitude = parseAngle(iso.left(split), 2);
            const auto longitude = parseAngle(iso.mid(split), 3);
            if (latitude && longitude)
                return Coordinates {*latitude, *longitude};
        }
    }
    return std::nullopt;
}

} // namespace NightLightSchedule
