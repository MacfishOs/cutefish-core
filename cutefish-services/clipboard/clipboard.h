#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <QClipboard>
#include <QObject>

class WaylandClipboard;

class Clipboard : public QObject
{
    Q_OBJECT

public:
    explicit Clipboard(QObject* parent = nullptr);

private slots:
    void onDataChanged();

private:
    QClipboard* m_qtClipboard = nullptr;
    WaylandClipboard* m_waylandClipboard = nullptr;
};

#endif // CLIPBOARD_H
