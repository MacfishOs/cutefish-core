#include "permissionprompt.h"
#include "desktopentry.h"
#include "waylandparent.h"

#include <QDBusArgument>
#include <QDBusVariant>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>

namespace {

struct Kind {
    const char *name;
    const char *content;
    const char *icon;
};

const Kind kinds[] = {
        {"screen-recording", "ScreenRecordingContent.qml", "video-display"},
        {"computer-control", "ComputerControlContent.qml", "preferences-desktop-remote-desktop"},
};

const Kind *findKind(const QString &name)
{
    for (const auto &kind : kinds) {
        if (name == QLatin1String(kind.name))
            return &kind;
    }
    return nullptr;
}

// D-Bus containers arrive as QDBusArgument; QML wants plain values. Nested
// dictionaries are pulled out as maps explicitly: QtDBus does not turn an
// element of an `aa{sv}` into anything usable on its own.
QVariant plain(const QVariant &value)
{
    if (value.userType() == qMetaTypeId<QDBusArgument>()) {
        const auto argument = value.value<QDBusArgument>();
        const QString signature = argument.currentSignature();
        if (signature.startsWith(QLatin1String("a{"))) {
            QVariantMap map;
            argument >> map;
            return plain(map);
        }
        if (signature.startsWith(QLatin1Char('a'))) {
            const bool maps = signature.mid(1).startsWith(QLatin1String("a{"));
            QVariantList list;
            argument.beginArray();
            while (!argument.atEnd()) {
                if (maps) {
                    QVariantMap map;
                    argument >> map;
                    list.append(plain(map));
                } else {
                    QVariant entry;
                    argument >> entry;
                    list.append(plain(entry));
                }
            }
            argument.endArray();
            return list;
        }
        return QVariant();
    }
    if (value.userType() == qMetaTypeId<QDBusVariant>())
        return plain(value.value<QDBusVariant>().variant());
    if (value.userType() == QMetaType::QVariantMap) {
        QVariantMap result;
        const auto map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            result.insert(it.key(), plain(it.value()));
        return result;
    }
    if (value.userType() == QMetaType::QVariantList) {
        QVariantList result;
        for (const auto &entry : value.toList())
            result.append(plain(entry));
        return result;
    }
    return value;
}

// The application shown to the user comes from the caller's app id; an app
// without a desktop file is still named, just by its id.
QJsonObject describeApplication(const QString &appId)
{
    QJsonObject result {{"id", appId}, {"name", appId}, {"icon", QString()}};
    if (appId.isEmpty())
        return result;
    const QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                                appId + QStringLiteral(".desktop"));
    QFile file(path);
    DesktopEntryData data;
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)
        || !DesktopEntry::parse(appId, path, file.readAll(), &data))
        return result;
    if (!data.name.isEmpty())
        result.insert("name", data.name);
    result.insert("icon", data.icon);
    return result;
}

} // namespace

bool PermissionPrompt::supports(const QString &kind)
{
    return findKind(kind) != nullptr;
}

PermissionPrompt::PermissionPrompt(QQmlEngine *engine, const QString &kind, const QString &appId,
                                   const QString &parentWindow, const QVariantMap &options,
                                   const QDBusMessage &message, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_message(message)
    , m_kind(kind)
    , m_parentWindow(parentWindow)
    , m_application(describeApplication(appId))
    , m_request(QJsonObject::fromVariantMap(plain(options).toMap()))
{
    if (const Kind *entry = findKind(kind)) {
        m_content = QString::fromLatin1(entry->content);
        m_icon = QString::fromLatin1(entry->icon);
    }
    m_request.insert("app_id", appId);
    m_message.setDelayedReply(true);
}

PermissionPrompt::~PermissionPrompt()
{
    reply(2, {});
    delete m_window;
}

bool PermissionPrompt::show()
{
    QQmlComponent component(m_engine, QUrl(QStringLiteral("qrc:/permissions/PermissionPrompt.qml")));
    auto *context = new QQmlContext(m_engine->rootContext(), this);
    context->setContextProperty(QStringLiteral("prompt"), this);
    m_window = qobject_cast<QQuickWindow *>(component.create(context));
    if (!m_window) {
        qWarning() << "Cannot create the permission prompt:" << component.errorString();
        return false;
    }
    connect(m_window, &QQuickWindow::closing, this, &PermissionPrompt::deny);
    m_window->show();
    applyWaylandParent(m_window, m_parentWindow, true);
    m_window->requestActivate();
    return true;
}

void PermissionPrompt::dismiss()
{
    reply(1, {});
}

void PermissionPrompt::allow(const QJsonObject &details)
{
    QVariantMap results = details.toVariantMap();
    // JSON arrays of strings must go out as `as`, not `av`.
    for (auto it = results.begin(); it != results.end(); ++it) {
        if (it.value().userType() == QMetaType::QVariantList) {
            const auto list = it.value().toList();
            if (std::all_of(list.begin(), list.end(), [](const QVariant &v) {
                    return v.userType() == QMetaType::QString;
                }))
                it.value() = it.value().toStringList();
        }
    }
    reply(0, results);
}

void PermissionPrompt::deny()
{
    reply(1, {});
}

QString PermissionPrompt::applicationIcon(const QString &appId) const
{
    return describeApplication(appId).value("icon").toString();
}

void PermissionPrompt::reply(uint response, const QVariantMap &results)
{
    if (m_finished)
        return;
    m_finished = true;
    QDBusConnection::sessionBus().send(m_message.createReply({response, results}));
    if (m_window)
        m_window->hide();
    Q_EMIT finished();
}
