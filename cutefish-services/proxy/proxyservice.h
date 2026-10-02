#ifndef PROXYSERVICE_H
#define PROXYSERVICE_H

#include <QFileSystemWatcher>
#include <QMap>
#include <QObject>
#include <QTimer>

// Follows org.gnome.system.proxy, which Cutefish Settings and third-party proxy clients write,
// and republishes it to the environment of D-Bus and systemd activated programs.
// Applications started by AppRuntime read the keyfile themselves at launch.
class ProxyService : public QObject
{
    Q_OBJECT

public:
    explicit ProxyService(QObject *parent = nullptr);

private:
    void watch();
    void reload();
    void publishEnvironment();

    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
    QMap<QString, QString> m_environment;
    bool m_environmentPublished = false;
};

#endif
