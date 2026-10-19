// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "language.h"
#include "appearance/fontconfigwriter.h"

#include <algorithm>
#include <map>

#include <systemd/sd-bus.h>
#include <unicode/uloc.h>

#include "base/strings.h"
#include "common/environment.h"
#include "common/language_environment.h"

using namespace cutefish::base;
using namespace cutefish::base::strings;
namespace language = cutefish::core::language;

static void removeAll(std::vector<std::string> &list, const std::string &value)
{
    list.erase(std::remove(list.begin(), list.end(), value), list.end());
}

static void removeDuplicates(std::vector<std::string> &list)
{
    std::vector<std::string> unique;
    for (const std::string &entry : list) {
        if (std::find(unique.begin(), unique.end(), entry) == unique.end())
            unique.push_back(entry);
    }
    list = std::move(unique);
}

Language::Language(Settings &settings, dbus::Connection *session, Appearance appearance)
    : m_settings(settings)
    , m_session(session)
    , m_appearance(std::move(appearance))
{
}

Language::~Language() = default;

bool Language::publish()
{
    if (!m_session)
        return false;
    m_object = std::make_unique<dbus::Object>(m_session, "/org/cutefish/Services/Language",
                                              "org.cutefish.Services.Language");
    m_object->addMethod("setLanguage", [this](const std::string &value) { setLanguage(value); },
                        {{"value"}, {}});
    m_object->addMethod("setPreferredLanguages",
                        [this](const std::vector<std::string> &value) { setPreferredLanguages(value); },
                        {{"value"}, {}});
    m_object->addProperty("preferredLanguages", [this] { return preferredLanguages(); });
    m_object->addProperty("languageCode", [this] { return languageCode(); });
    m_object->addProperty("environment", [this] { return environment(); });
    m_object->addSignal("languageChanged", "");
    return m_object->publish();
}

std::string Language::languageCode() const
{
    const std::string code = m_settings.stringValue("Language/language");
    return code.empty() ? language::systemLanguage() : code;
}

std::vector<std::string> Language::preferredLanguages() const
{
    const std::string primary = languageCode();
    std::vector<std::string> codes = m_settings.stringListValue("Language/preferredLanguages");
    removeAll(codes, primary);
    codes.insert(codes.begin(), primary);
    removeDuplicates(codes);
    return codes;
}

std::map<std::string, std::string> Language::environment() const
{
    cutefish::core::Environment environment = cutefish::core::processEnvironment();
    language::apply(environment, preferredLanguages());
    std::map<std::string, std::string> variables;
    for (const auto &[name, value] : environment) {
        if (name == "LANG" || name == "LANGUAGE" || startsWith(name, "LC_"))
            variables[name] = value;
    }
    // D-Bus cannot remove variables; an empty LC_ALL lets individual categories take effect.
    variables["LC_ALL"] = std::string();
    return variables;
}

void Language::setLanguage(const std::string &code)
{
    std::vector<std::string> codes = preferredLanguages();
    removeAll(codes, code);
    codes.insert(codes.begin(), code);
    setPreferredLanguages(codes);
}

std::string Language::localeName(const std::string &code)
{
    char likely[ULOC_FULLNAME_CAPACITY] = {};
    UErrorCode status = U_ZERO_ERROR;
    uloc_addLikelySubtags(code.c_str(), likely, sizeof(likely), &status);
    if (U_FAILURE(status))
        return "C";
    char languageCode[ULOC_LANG_CAPACITY] = {};
    char country[ULOC_COUNTRY_CAPACITY] = {};
    uloc_getLanguage(likely, languageCode, sizeof(languageCode), &status);
    uloc_getCountry(likely, country, sizeof(country), &status);
    if (U_FAILURE(status) || !*languageCode || std::string_view(languageCode) == "c")
        return "C";
    return *country ? std::string(languageCode) + '_' + country : std::string(languageCode);
}

void Language::setPreferredLanguages(const std::vector<std::string> &languages)
{
    std::vector<std::string> codes = languages;
    removeAll(codes, std::string());
    removeDuplicates(codes);
    if (codes.empty() || codes == preferredLanguages())
        return;

    const std::string code = codes.front();
    const bool primaryChanged = code != languageCode();
    m_settings.setValue("Language/preferredLanguages", codes);

    if (primaryChanged) {
        const std::string font = fontForLocale(localeName(code));
        if (!font.empty() && m_appearance.setSystemFont)
            m_appearance.setSystemFont(font);
    }

    m_settings.setValue("Language/language", code);
    m_settings.sync();
    // The CJK fallback follows the order of the languages, so it is rewritten after the sync.
    if (m_appearance.updateFontConfig)
        m_appearance.updateFontConfig();

    const std::map<std::string, std::string> activationEnvironment = environment();
    std::vector<std::string> systemdEnvironment;
    for (const auto &[name, value] : activationEnvironment) {
        if (name != "LC_ALL")
            systemdEnvironment.push_back(name + '=' + value);
    }
    if (m_session) {
        const dbus::Proxy bus(m_session, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                              "org.freedesktop.DBus");
        bus.send("UpdateActivationEnvironment", activationEnvironment);

        dbus::Message systemd = m_session->newMethodCall("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                                                         "org.freedesktop.systemd1.Manager",
                                                         "UnsetAndSetEnvironment");
        sd_bus_message_set_auto_start(systemd.get(), 0);
        systemd.append(std::vector<std::string> {"LC_ALL"}, systemdEnvironment);
        m_session->send(systemd);
    }
    emitLanguageChanged();

    if (!primaryChanged)
        return;

    m_notification = std::make_unique<Notification>(m_session);
    m_notification->setDesktopEntry("cutefish-settings");
    m_notification->setIconName("preferences-system");
    m_notification->setBody("Some applications may need to be restarted for the change to take effect.");
    m_notification->setTimeout(10);
    m_notification->show();
}

void Language::emitLanguageChanged()
{
    if (m_object)
        m_object->emitSignal("languageChanged");
}
