// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef APPLICATION_H
#define APPLICATION_H

#include "appearance/thememanager.h"
#include "battery/upowermanager.h"
#include "brightness/brightnessmanager.h"
#include "clipboard/clipboard.h"
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
    ThemeManager *m_themeManager;
    BrightnessManager *m_brightnessManager;
    UPowerManager *m_upowerManager;
    Language *m_language;
    Clipboard *m_clipboard;
    PowerManager *m_powerManager;
    NightLight *m_nightLight;
    PermissionManager *m_permissionManager;
    ProxyService *m_proxyService;
};

#endif // APPLICATION_H
