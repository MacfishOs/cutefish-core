#include "xwaylandenvironment.h"

#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/json.h"
#include "base/paths.h"
#include "logs.h"

static bool isLocalDisplay(const std::string& display)
{
    if (display.size() < 2 || display.front() != ':')
        return false;
    for (size_t i = 1; i < display.size(); ++i) {
        if (display[i] < '0' || display[i] > '9')
            return false;
    }
    return true;
}

XwaylandEnvironment::XwaylandEnvironment()
    : m_directory(cutefish::base::paths::runtimeDirectory(), "cutefish-session-x11-")
{
    m_timeout.setSingleShot(true);
    m_timeout.onTimeout = [this] {
        m_waiting = false;
        CF_WARNING(sessionLog()) << "Xwayland readiness timed out; continuing with native Wayland";
        if (onResolved)
            onResolved(false);
    };
    if (m_directory.isValid())
        m_watch = cutefish::base::FileWatch::directory(m_directory.path(), [this](const std::string&) {
            check();
        });
}

std::string XwaylandEnvironment::readinessFile() const
{
    return m_directory.isValid() ? m_directory.path() + "/ready.json" : std::string();
}

void XwaylandEnvironment::waitForReady(int timeoutMs)
{
    unsetenv("DISPLAY");
    unsetenv("XAUTHORITY");
    m_waiting = true;
    m_timeout.start(std::chrono::milliseconds(timeoutMs));
    check();
}

void XwaylandEnvironment::check()
{
    const std::string path = readinessFile();
    if (path.empty() || ::access(path.c_str(), F_OK) != 0) {
        if (m_available) {
            m_available = false;
            unsetenv("DISPLAY");
            unsetenv("XAUTHORITY");
            if (onLost)
                onLost();
        }
        return;
    }
    if (!m_waiting)
        return;
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return;
    std::string bytes(4097, '\0');
    size_t length = 0;
    while (length < bytes.size()) {
        const ssize_t n = ::read(fd, bytes.data() + length, bytes.size() - length);
        if (n <= 0)
            break;
        length += size_t(n);
    }
    ::close(fd);
    if (length > 4096)
        return;
    bytes.resize(length);
    const auto document = cutefish::base::json::Value::parse(bytes);
    if (!document || !document->isObject())
        return;
    const std::string display = (*document)["DISPLAY"].toString();
    const std::string authority = (*document)["XAUTHORITY"].toString();
    struct stat st {};
    if (!isLocalDisplay(display) || !authority.starts_with('/') || authority.find('\0') != std::string::npos ||
        lstat(authority.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
        (st.st_mode & 0777) != 0600)
        return;
    setenv("DISPLAY", display.c_str(), 1);
    setenv("XAUTHORITY", authority.c_str(), 1);
    m_waiting = false;
    m_available = true;
    m_timeout.stop();
    if (onResolved)
        onResolved(true);
}
