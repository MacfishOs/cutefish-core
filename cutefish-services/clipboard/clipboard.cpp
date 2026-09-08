#include "clipboard.h"

#include <QGuiApplication>
#include <QMimeData>
#include <QVariant>

#include "waylandclipboard.h"

Clipboard::Clipboard(QObject* parent)
    : QObject(parent)
    , m_qtClipboard(QGuiApplication::clipboard())
{
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        m_waylandClipboard = new WaylandClipboard(this);
        connect(m_waylandClipboard, &WaylandClipboard::dataChanged, this,
                &Clipboard::onDataChanged);
        connect(m_waylandClipboard, &WaylandClipboard::availabilityChanged, this,
                &Clipboard::onDataChanged);
    }

    if (m_qtClipboard) {
        connect(m_qtClipboard, &QClipboard::dataChanged, this, &Clipboard::onDataChanged);
    }
}

void Clipboard::onDataChanged()
{
    const bool useWaylandClipboard = m_waylandClipboard && m_waylandClipboard->isAvailable();
    const QMimeData* mimeData = useWaylandClipboard
        ? m_waylandClipboard->mimeData()
        : (m_qtClipboard ? m_qtClipboard->mimeData(QClipboard::Clipboard) : nullptr);

    if (!mimeData || mimeData->formats().isEmpty())
        return;

    if (mimeData->hasFormat(QStringLiteral("application/x-cutefish-clipboard")) &&
        mimeData->data(QStringLiteral("application/x-cutefish-clipboard")) ==
            QByteArrayLiteral("1"))
        return;

    QMimeData* newMimeData = new QMimeData;
    for (const QString& format : mimeData->formats())
        newMimeData->setData(format, mimeData->data(format));
    if (!useWaylandClipboard && mimeData->hasImage())
        newMimeData->setImageData(mimeData->imageData());

    newMimeData->setData(QStringLiteral("application/x-cutefish-clipboard"),
                         QByteArrayLiteral("1"));

    if (useWaylandClipboard) {
        m_waylandClipboard->setMimeData(newMimeData);
    } else if (m_qtClipboard) {
        m_qtClipboard->setMimeData(newMimeData, QClipboard::Clipboard);
    } else {
        delete newMimeData;
    }
}
