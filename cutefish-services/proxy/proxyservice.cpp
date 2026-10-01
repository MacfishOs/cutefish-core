#include "proxyservice.h"
#include "keyfile.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDir>
#include <QFile>
#include <QFileInfo>

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
    publishEnvironment();
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
