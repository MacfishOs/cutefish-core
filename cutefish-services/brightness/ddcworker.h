#ifndef DDCWORKER_H
#define DDCWORKER_H

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QMutex>
#include <QObject>

struct DdcDisplay {
    QString connector;
    QString device;
    int value = 0;
    int maximum = 0;
};

Q_DECLARE_METATYPE(DdcDisplay)
Q_DECLARE_METATYPE(QList<DdcDisplay>)

// Blocking DDC/CI I/O; lives on its own thread because each transaction takes tens of ms.
class DdcWorker : public QObject
{
    Q_OBJECT

public:
    explicit DdcWorker(QObject *parent = nullptr);

    // Thread-safe. Only the latest value per device is written.
    void requestValue(const QString &device, int value);

public slots:
    void probe(quint64 generation, const QList<DdcDisplay> &candidates);

signals:
    void probed(quint64 generation, const QList<DdcDisplay> &displays);

private slots:
    void flush();

private:
    QMutex m_mutex;
    QHash<QString, int> m_pending;
    bool m_flushQueued = false;
};

#endif
