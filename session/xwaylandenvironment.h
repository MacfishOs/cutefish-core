#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTemporaryDir>
#include <QTimer>

class XwaylandEnvironment final : public QObject
{
    Q_OBJECT
public:
    explicit XwaylandEnvironment(QObject* parent = nullptr);
    QString readinessFile() const;
    void waitForReady(int timeoutMs = 12000);
signals:
    void resolved(bool available);
    void lost();

private:
    void check();
    QTemporaryDir m_directory;
    QFileSystemWatcher m_watcher;
    QTimer m_timeout;
    bool m_waiting = false;
    bool m_available = false;
};
