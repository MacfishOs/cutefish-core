#pragma once

#include <QObject>
#include <QProcess>
#include <QSocketNotifier>
#include <QTimer>

// Session lifecycle only. No window-management or shell UI traffic crosses here.
class CompositorProcess final : public QObject
{
    Q_OBJECT
public:
    explicit CompositorProcess(QObject* parent = nullptr);
    ~CompositorProcess() override;
    void start(const QString& program, const QString& socket, const QString& controlSocket,
               int timeoutMs = 15000, const QString& xwaylandReadyFile = {});
    void stop();

    bool isReady() const { return m_ready; }

signals:
    void ready();
    void failed(const QString& reason);
    void exited(int code);

private:
    void closeReadyPipe();
    void fail(const QString& reason);
    QProcess m_process;
    QTimer m_timeout;
    QSocketNotifier* m_notifier = nullptr;
    int m_readFd = -1;
    bool m_ready = false;
    bool m_stopping = false;
    bool m_failed = false;
};
