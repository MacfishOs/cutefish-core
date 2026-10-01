// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NIGHTLIGHTSCHEDULE_H
#define NIGHTLIGHTSCHEDULE_H

#include <QByteArray>
#include <QDateTime>
#include <QStringList>
#include <QTime>
#include <QTimeZone>

#include <functional>
#include <optional>

// Where a day stands on the night light's schedule, and the sunset and sunrise
// it follows. Modelled on Ash's schedule_utils::GetCurrentPosition() and
// GeolocationController, with the location taken from the time zone.
namespace NightLightSchedule {

// The times of day the night light turns on and off.
struct Times {
    QTime start;
    QTime end;
};

using TimesForDate = std::function<Times(QDate)>;

struct Position {
    // What the schedule says now.
    bool enabled = false;
    // When it next says the opposite; invalid when it never turns on.
    QDateTime next;
};

// On from `start` to `end` every day, across midnight when `end` comes first.
// Equal times never turn it on.
Position position(const QDateTime &now, const TimesForDate &times);

struct SunTimes {
    QDateTime sunrise;
    QDateTime sunset;
};

// NOAA's sunrise equation, good to a few minutes, for `date` at a latitude and
// longitude in degrees, north and east positive, as times in `zone`. Empty on a
// day the sun does not rise or does not set.
std::optional<SunTimes> sunTimes(QDate date, double latitude, double longitude,
                                 const QTimeZone &zone);

struct Coordinates {
    double latitude = 0;
    double longitude = 0;
};

// tzdata's zone1970.tab and zone.tab.
QStringList zoneTables();

// Where `tables` place `zoneId`'s principal city. Empty for a zone they do not
// list, such as UTC.
std::optional<Coordinates> zoneCoordinates(const QByteArray &zoneId,
                                           const QStringList &tables = zoneTables());

} // namespace NightLightSchedule

#endif // NIGHTLIGHTSCHEDULE_H
