#include "applicationscope.h"

#include "base/dbus.h"
#include "base/event_loop.h"
#include "base/logging.h"

#include <cstdio>
#include <memory>
#include <tuple>
#include <vector>

#include <signal.h>

namespace dbus = cutefish::base::dbus;

static const cutefish::base::LogCategory kLog("cutefish.appruntime");

// (sv) and (sa(sv)): unit properties and auxiliary units.
using UnitProperty = std::tuple<std::string, dbus::Variant>;
using AuxiliaryUnit = std::tuple<std::string, std::vector<UnitProperty>>;

// systemd's unit name escaping: [a-zA-Z0-9:_.] pass, a leading '.' and
// everything else become \xNN of the UTF-8 byte.
static std::string escape(const std::string &text)
{
    std::string result;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == ':' || c == '_' || (c == '.' && i > 0);
        if (plain) {
            result += c;
        } else {
            char escaped[5];
            std::snprintf(escaped, sizeof(escaped), "\\x%02x", static_cast<unsigned char>(c));
            result += escaped;
        }
    }
    return result;
}

static dbus::Message managerCall(dbus::Connection *bus, const char *method)
{
    return bus->newMethodCall("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                              "org.freedesktop.systemd1.Manager", method);
}

std::string ApplicationScope::unitName(const std::string &appId, uint32_t pid)
{
    return "app-cutefish-" + escape(appId) + '-' + std::to_string(pid) + ".scope";
}

void ApplicationScope::adopt(const std::string &appId, uint32_t pid)
{
    if (appId.empty() || pid == 0)
        return;
    dbus::Connection *bus = dbus::Connection::session();
    if (!bus)
        return;
    const std::vector<UnitProperty> properties {
            {"PIDs", dbus::Variant(std::vector<uint32_t> {pid})},
            {"CollectMode", dbus::Variant(std::string("inactive-or-failed"))},
    };
    dbus::Message call = managerCall(bus, "StartTransientUnit");
    call.append(unitName(appId, pid), std::string("fail"), properties, std::vector<AuxiliaryUnit>());
    // Kept until the reply has been handled, then released on the loop.
    auto pending = std::make_shared<dbus::PendingCall>();
    *pending = bus->callAsync(call, [pending, appId, pid](const dbus::Reply &reply) mutable {
        if (reply.isError())
            CF_WARNING(kLog) << "AppRuntime: no systemd scope for" << appId << pid << reply.error().message;
        cutefish::base::EventLoop::current()->post([released = std::move(pending)] {});
    });
}

void ApplicationScope::killAll(const std::string &unitName)
{
    dbus::Connection *bus = dbus::Connection::session();
    if (!bus)
        return;
    dbus::Message call = managerCall(bus, "KillUnit");
    call.append(unitName, std::string("all"), int32_t(SIGKILL));
    bus->send(call);
}
