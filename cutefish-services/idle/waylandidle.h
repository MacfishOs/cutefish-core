#pragma once

#include <QObject>
#include <memory>

class WaylandIdle : public QObject
{
    Q_OBJECT
public:
    explicit WaylandIdle(QObject *parent = nullptr);
    ~WaylandIdle() override;
    int addTimeout(int milliseconds);
    void removeTimeout(int id);

Q_SIGNALS:
    void timeoutReached(int id, int milliseconds);
    void resumed();

private:
    class Private;
    std::unique_ptr<Private> d;
};
