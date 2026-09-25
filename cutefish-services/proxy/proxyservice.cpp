#include "proxyservice.h"
#include "keyfile.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace
{
QString kioServer(const QString &scheme, const ProxySettings::Server &server)
{
    if (!server.isValid())
        return QString();

    QString host = server.host;
    if (host.contains(QLatin1Char(':')) && !host.startsWith(QLatin1Char('[')))
        host = QLatin1Char('[') + host + QLatin1Char(']');
    // KIO's own format: the port follows the URL after a space.
    return scheme + QLatin1String("://") + host + QLatin1Char(' ') + QString::number(server.port);
}

int kioProxyType(ProxySettings::Mode mode)
{
    switch (mode) {
    case ProxySettings::Manual:
        return 1;
    case ProxySettings::Auto:
        return 2;
    default:
        return 0;
    }
}
}

ProxyService::ProxyService(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<QMap<QString, QString>>();

    // gsettings and QSaveFile both replace the file by rename, which drops a file watch.
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(100);
    connect(&m_reloadTimer, &QTimer::timeout, this, &ProxyService::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &ProxyService::watch);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &ProxyService::watch);

    const QString path = GSettingsKeyfile::path();
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_watcher.addPath(QFileInfo(path).absolutePath());
    if (QFile::exists(path))
        m_watcher.addPath(path);
    reload();
}

void ProxyService::watch()
{
    const QString path = GSettingsKeyfile::path();
    if (QFile::exists(path) && !m_watcher.files().contains(path))
        m_watcher.addPath(path);
    m_reloadTimer.start();
}

void ProxyService::reload()
{
    const ProxySettings settings = ProxySettings::load();
    if (m_loaded && settings == m_settings)
        return;

    m_loaded = true;
    m_settings = settings;
    writeKioConfig();
    publishEnvironment();
}

void ProxyService::writeKioConfig()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QLatin1String("/kioslaverc");
    if (m_settings.mode == ProxySettings::None && !QFile::exists(path))
        return;

    const bool changed = KeyFile::writeGroup(path, QStringLiteral("Proxy Settings"), {
        {QStringLiteral("ProxyType"), QString::number(kioProxyType(m_settings.mode))},
        {QStringLiteral("httpProxy"), kioServer(QStringLiteral("http"), m_settings.http)},
        {QStringLiteral("httpsProxy"),
         kioServer(QStringLiteral("http"), m_settings.effectiveHttps())},
        {QStringLiteral("ftpProxy"), kioServer(QStringLiteral("http"), m_settings.ftp)},
        {QStringLiteral("socksProxy"), kioServer(QStringLiteral("socks"), m_settings.socks)},
        {QStringLiteral("Proxy Config Script"), m_settings.autoconfigUrl},
        {QStringLiteral("NoProxyFor"), m_settings.ignoreHosts.join(QLatin1Char(','))},
        {QStringLiteral("ReversedException"), QStringLiteral("false")},
    });
    if (!changed)
        return;

    QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/KIO/Scheduler"),
                                                      QStringLiteral("org.kde.KIO.Scheduler"),
                                                      QStringLiteral("reparseSlaveConfiguration"));
    message << QString();
    QDBusConnection::sessionBus().send(message);
}

void ProxyService::publishEnvironment()
{
    const QMap<QString, QString> variables = ProxyEnvironment::variables(m_settings);
    // The first pass always publishes: systemd keeps what an earlier login exported.
    if (m_environmentPublished && variables == m_environment)
        return;
    m_environmentPublished = true;
    m_environment = variables;

    QStringList unset;
    QStringList assignments;
    QMap<QString, QString> activation;
    for (const QString &name : ProxyEnvironment::names()) {
        const QString value = variables.value(name);
        if (variables.contains(name))
            assignments << name + QLatin1Char('=') + value;
        else
            unset << name;
        // dbus-daemon cannot unset; an empty proxy variable means none to every reader.
        activation.insert(name, value);
    }

    QDBusMessage systemd = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
            QStringLiteral("org.freedesktop.systemd1.Manager"),
            QStringLiteral("UnsetAndSetEnvironment"));
    systemd << unset << assignments;
    QDBusConnection::sessionBus().send(systemd);

    QDBusMessage dbus = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("UpdateActivationEnvironment"));
    dbus << QVariant::fromValue(activation);
    QDBusConnection::sessionBus().send(dbus);
}
