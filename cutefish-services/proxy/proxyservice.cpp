#include "proxyservice.h"
#include "keyfile.h"
#include "proxysettings.h"

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
    const QMap<QString, QString> variables = ProxyEnvironment::variables(ProxySettings::load());
    // The first pass always publishes: systemd keeps what an earlier login exported.
    if (m_environmentPublished && variables == m_environment)
        return;
    m_environmentPublished = true;
    m_environment = variables;

    publishEnvironment();
}

void ProxyService::publishEnvironment()
{
    QStringList unset;
    QStringList assignments;
    QMap<QString, QString> activation;
    for (const QString &name : ProxyEnvironment::names()) {
        const QString value = m_environment.value(name);
        if (m_environment.contains(name))
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
