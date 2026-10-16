#ifndef PROXYSERVICE_H
#define PROXYSERVICE_H

#include <map>
#include <string>

#include "base/dbus.h"
#include "base/file_watcher.h"
#include "base/timer.h"

// Follows org.gnome.system.proxy, which Cutefish Settings and third-party proxy clients write,
// and republishes it to the environment of D-Bus and systemd activated programs.
// Applications started by AppRuntime read the keyfile themselves at launch.
class ProxyService
{
public:
    explicit ProxyService(cutefish::base::dbus::Connection *session);
    ~ProxyService();

    ProxyService(const ProxyService &) = delete;
    ProxyService &operator=(const ProxyService &) = delete;

private:
    void reload();
    void publishEnvironment();

    cutefish::base::dbus::Connection *m_session;
    cutefish::base::FileWatch m_watch;
    cutefish::base::Timer m_reloadTimer;
    std::map<std::string, std::string> m_environment;
    bool m_environmentPublished = false;
};

#endif
