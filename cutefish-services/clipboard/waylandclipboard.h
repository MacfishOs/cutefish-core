#ifndef WAYLANDCLIPBOARD_H
#define WAYLANDCLIPBOARD_H

#include <QObject>

class QMimeData;

class WaylandClipboard : public QObject
{
    Q_OBJECT

public:
    explicit WaylandClipboard(QObject* parent = nullptr);
    ~WaylandClipboard() override;

    bool isAvailable() const;
    const QMimeData* mimeData() const;
    void setMimeData(QMimeData* mimeData);

Q_SIGNALS:
    void availabilityChanged();
    void dataChanged();

private:
    class Private;
    Private* d;
};

#endif // WAYLANDCLIPBOARD_H
