// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef APPLICATION_H
#define APPLICATION_H

#include "appearance/thememanager.h"
#include "battery/batterywarning.h"
#include "battery/upowermanager.h"
#include "brightness/brightnessmanager.h"
#include "clipboard/clipboard.h"
#include "crash/crashmanager.h"
#include "locale/language.h"
#include "nightlight/nightlight.h"
#include "permissions/permissionmanager.h"
#include "power/powermanager.h"
#include "proxy/proxyservice.h"
#include <QGuiApplication>

class Application : public QGuiApplication
{
    Q_OBJECT

public:
    explicit Application(int &argc, char **argv);

    void initTrash();

private:
    // One engine for every window of this process: FishUI sets up only the
    // first engine that imports it.
    QQmlEngine *engine();

    QQmlEngine *m_engine = nullptr;
    ThemeManager *m_themeManager;
    BrightnessManager *m_brightnessManager;
    UPowerManager *m_upowerManager;
    BatteryWarning *m_batteryWarning;
    Language *m_language;
    Clipboard *m_clipboard;
    PowerManager *m_powerManager;
    NightLight *m_nightLight;
    PermissionManager *m_permissionManager;
    CrashManager *m_crashManager;
    ProxyService *m_proxyService;
};

#endif // APPLICATION_H
