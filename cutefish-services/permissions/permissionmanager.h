#pragma once

#include <map>
#include <memory>
#include <string>

#include "base/dbus.h"

class PermissionPrompt;

// com.cutefish.Services.Permissions: shows permission prompts on behalf of
// portals and services, which only speak D-Bus and never draw. One prompt per
// token; the answer is the delayed reply to Prompt.
class PermissionManager
{
public:
    explicit PermissionManager(cutefish::base::dbus::Connection *session);
    ~PermissionManager();

    PermissionManager(const PermissionManager &) = delete;
    PermissionManager &operator=(const PermissionManager &) = delete;

    // /com/cutefish/Services/Permissions.
    bool publish();

    // The prompt a token opened, for tests; null when none is open.
    PermissionPrompt *prompt(const std::string &token) const;

private:
    // Replies (response, results): 0 allowed, 1 denied or dismissed, 2 failed.
    // results carries whatever the kind's content chose.
    void prompt(cutefish::base::dbus::MethodCall &call);
    void dismiss(cutefish::base::dbus::MethodCall &call);
    void remove(const std::string &token);

    cutefish::base::dbus::Connection *m_session;
    std::unique_ptr<cutefish::base::dbus::Object> m_object;
    std::map<std::string, std::unique_ptr<PermissionPrompt>> m_prompts;
};
