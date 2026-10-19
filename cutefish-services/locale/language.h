// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#ifndef LANGUAGE_H
#define LANGUAGE_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/settings.h"
#include "notification.h"

class Language
{
public:
    // What a language change does to the appearance: a primary language with
    // a font of its own sets it, and the fallback families follow the order.
    struct Appearance {
        std::function<void(const std::string &family)> setSystemFont;
        std::function<void()> updateFontConfig;
    };

    Language(cutefish::base::Settings &settings, cutefish::base::dbus::Connection *session,
             Appearance appearance);
    ~Language();

    Language(const Language &) = delete;
    Language &operator=(const Language &) = delete;

    // com.cutefish.Services.Language at /com/cutefish/Services/Language.
    bool publish();

    std::string languageCode() const;
    void setLanguage(const std::string &code);
    std::vector<std::string> preferredLanguages() const;
    void setPreferredLanguages(const std::vector<std::string> &codes);

    // A locale's language and territory, such as "en_US" for "en", as the
    // session names it.
    static std::string localeName(const std::string &code);

private:
    void emitLanguageChanged();

    cutefish::base::Settings &m_settings;
    cutefish::base::dbus::Connection *m_session;
    Appearance m_appearance;
    std::unique_ptr<cutefish::base::dbus::Object> m_object;
    std::unique_ptr<Notification> m_notification;
};

#endif
