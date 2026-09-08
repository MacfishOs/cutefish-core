#pragma once

#include <QStringList>

// Directories are ordered from highest to lowest precedence.
QStringList autostartEntries(const QStringList& directories, const QStringList& desktops);
