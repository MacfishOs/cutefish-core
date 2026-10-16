// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application.h"

#include "appearance/thememanager.h"
#include "battery/batterywarning.h"
#include "battery/upowermanager.h"
#include "brightness/brightnessmanager.h"
#include "clipboard/clipboard.h"
#include "crash/crashmanager.h"
#include "locale/language.h"
#include "nightlight/nightlight.h"
#include "permissions/permissionmanager.h"
#include "power/idlemanager.h"
#include "power/powermanager.h"
#include "proxy/proxyservice.h"

#include "base/dbus.h"
#include "base/paths.h"
#include "ui/theme.h"
#include "log.h"

#include <iterator>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using namespace cutefish::base;
namespace ui = cutefish::ui;

Application::Application()
    : m_session(dbus::Connection::session())
    , m_system(dbus::Connection::system())
    , m_settings(paths::cutefishConfig("services"))
{
    m_idleManager = std::make_unique<IdleManager>();
    m_themeManager = std::make_unique<ThemeManager>(m_settings, m_session);
    m_brightnessManager = std::make_unique<BrightnessManager>(m_session, m_system);
    m_upowerManager = std::make_unique<UPowerManager>(m_system, m_session, m_settings);
    m_batteryWarning = std::make_unique<BatteryWarning>(m_system, m_session);
    m_language = std::make_unique<Language>(
            m_settings, m_session,
            Language::Appearance {[this](const std::string &family) { m_themeManager->setSystemFont(family); },
                                  [this] { m_themeManager->updateFontConfig(); }});
    m_clipboard = std::make_unique<Clipboard>();
    m_powerManager = std::make_unique<PowerManager>(m_upowerManager.get(), m_brightnessManager.get(),
                                                    m_idleManager.get(), m_settings, m_session, m_system);
    m_nightLight = std::make_unique<NightLight>(m_settings);
    m_permissionManager = std::make_unique<PermissionManager>(m_session);
    m_crashManager = std::make_unique<CrashManager>([this] { return m_themeManager->systemFixedFont(); });
    m_proxyService = std::make_unique<ProxyService>(m_session);

    initTrash();

    applyTheme();
    m_themeChanged = m_themeManager->changed.connect([this] {
        applyTheme();
    });

    // Register all service objects before publishing the well-known name.
    m_themeManager->publish();
    m_brightnessManager->publish();
    m_language->publish();
    m_powerManager->publish();
    m_nightLight->publish(m_session);
    m_permissionManager->publish();

    if (!m_session || !m_session->requestName("com.cutefish.Services"))
        CF_WARNING(services::log()) << "Cannot own com.cutefish.Services";
}

Application::~Application()
{
    // The actions go before the idle manager they registered with.
    m_powerManager.reset();
}

void Application::applyTheme()
{
    // The accents, in the order of the setting's index.
    static const ui::Color accents[] = {
        ui::accent::blue, ui::accent::red, ui::accent::green, ui::accent::purple,
        ui::accent::pink, ui::accent::orange, ui::accent::grey,
    };

    ThemeManager &appearance = *m_themeManager;
    ui::Theme theme = appearance.isDarkMode() ? ui::Theme::dark() : ui::Theme::light();
    const int accent = appearance.accentColor();
    if (accent >= 0 && accent < int(std::size(accents)))
        theme.highlight = accents[accent];
    if (appearance.systemFontPointSize() > 0)
        theme.fontPointSize = appearance.systemFontPointSize();
    theme.fontFamily = appearance.systemFont();
    theme.fixedFontFamily = appearance.systemFixedFont();
    if (!appearance.iconTheme().empty())
        theme.iconTheme = appearance.iconTheme();
    theme.continuousCorners = appearance.continuousCorners();
    ui::Theme::setCurrent(theme);
}

void Application::initTrash()
{
    const std::string trashDir = paths::dataHome() + "/Trash";

    if (::mkdir(trashDir.c_str(), 0700) != 0)
        return;

    struct stat buff;
    uid_t uid = getuid();

    if (::lstat(trashDir.c_str(), &buff) != 0)
        return;

    if ((buff.st_uid == uid) && // must be owned by user
        ((buff.st_mode & 0777) == 0700)) {
        // check subdirs
        const std::string infoDir = trashDir + "/info";
        const std::string filesDir = trashDir + "/files";

        ::mkdir(infoDir.c_str(), 0777);
        ::mkdir(filesDir.c_str(), 0777);
    } else {
        ::rmdir(trashDir.c_str());
    }
}
