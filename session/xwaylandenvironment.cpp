#include "xwaylandenvironment.h"

#include <sys/stat.h>
#include <unistd.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

XwaylandEnvironment::XwaylandEnvironment(QObject* parent)
    : QObject(parent)
    , m_directory(QDir(qEnvironmentVariable("XDG_RUNTIME_DIR", QDir::tempPath()))
                      .filePath("cutefish-session-x11-XXXXXX"))
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_waiting = false;
        qWarning() << "Xwayland readiness timed out; continuing with native Wayland";
        emit resolved(false);
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &XwaylandEnvironment::check);
    if (m_directory.isValid())
        m_watcher.addPath(m_directory.path());
}

QString XwaylandEnvironment::readinessFile() const
{
    return m_directory.isValid() ? m_directory.filePath("ready.json") : QString();
}

void XwaylandEnvironment::waitForReady(int timeoutMs)
{
    qunsetenv("DISPLAY");
    qunsetenv("XAUTHORITY");
    m_waiting = true;
    m_timeout.start(timeoutMs);
    check();
}

void XwaylandEnvironment::check()
{
    QFile file(readinessFile());
    if (!file.exists()) {
        if (m_available) {
            m_available = false;
            qunsetenv("DISPLAY");
            qunsetenv("XAUTHORITY");
            emit lost();
        }
        return;
    }
    if (!m_waiting || !file.open(QIODevice::ReadOnly))
        return;
    const auto bytes = file.read(4097);
    if (bytes.size() > 4096)
        return;
    const auto document = QJsonDocument::fromJson(bytes);
    const auto object = document.object();
    const auto display = object.value("DISPLAY").toString();
    const auto authority = object.value("XAUTHORITY").toString();
    static const QRegularExpression displayPattern(QStringLiteral("^:[0-9]+$"));
    struct stat st {};
    if (!displayPattern.match(display).hasMatch() || !QDir::isAbsolutePath(authority) ||
        authority.contains(QChar::Null) ||
        lstat(QFile::encodeName(authority).constData(), &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_uid != getuid() || (st.st_mode & 0777) != 0600)
        return;
    qputenv("DISPLAY", display.toUtf8());
    qputenv("XAUTHORITY", authority.toUtf8());
    m_waiting = false;
    m_available = true;
    m_timeout.stop();
    emit resolved(true);
}
