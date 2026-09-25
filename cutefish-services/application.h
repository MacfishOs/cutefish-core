/*
 * Copyright (C) 2021 CutefishOS Team.
 *
 * Author:     revenmartin <revenmartin@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef APPLICATION_H
#define APPLICATION_H

#include "appearance/thememanager.h"
#include "battery/upowermanager.h"
#include "brightness/brightnessmanager.h"
#include "clipboard/clipboard.h"
#include "locale/language.h"
#include "permissions/permissionmanager.h"
#include "power/cpumanagement.h"
#include "power/powermanager.h"
#include "proxy/proxyservice.h"
#include <QApplication>

class Application : public QApplication
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
    CPUManagement *m_cpuManagement;
    PowerManager *m_powerManager;
    PermissionManager *m_permissionManager;
    ProxyService *m_proxyService;
};

#endif // APPLICATION_H
