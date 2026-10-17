// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LOGINBACKGROUND_H
#define LOGINBACKGROUND_H

#include <vector>

#include "base/dbus.h"
#include "base/signal.h"
#include "base/timer.h"

class WallpaperManager;

/**
 * Hands the desktop background to cutefish-dm, which keeps a copy the login
 * screen can read: once at login, then after every change. The picture goes
 * as an open file, so the display manager reads only what this user can.
 */
class LoginBackground
{
public:
    LoginBackground(WallpaperManager &wallpapers, cutefish::base::dbus::Connection *system);

    LoginBackground(const LoginBackground &) = delete;
    LoginBackground &operator=(const LoginBackground &) = delete;

private:
    void send();

    WallpaperManager &m_wallpapers;
    cutefish::base::dbus::Connection *m_system;
    // A wallpaper, its type and its colour often change together: one send.
    cutefish::base::Timer m_pending;
    std::vector<cutefish::base::Subscription> m_subscriptions;
};

#endif // LOGINBACKGROUND_H
