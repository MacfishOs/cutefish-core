#include "permissionmanager.h"
#include "permissionprompt.h"

#include <QDBusServiceWatcher>
#include <QQmlEngine>

namespace {

void error(const QDBusMessage &message, const QString &name, const QString &description)
{
    message.setDelayedReply(true);
    QDBusConnection::sessionBus().send(message.createErrorReply(name, description));
}

} // namespace

PermissionManager::PermissionManager(QObject *parent)
    : QObject(parent)
{
    // The QML lives in this static library's resources.
    Q_INIT_RESOURCE(permissions);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/com/cutefish/Services/Permissions"),
                                                 this, QDBusConnection::ExportAllSlots);
}

PermissionManager::~PermissionManager()
{
    qDeleteAll(m_prompts);
}

QQmlEngine *PermissionManager::engine()
{
    if (!m_engine) {
        m_engine = new QQmlEngine(this);
    }
    return m_engine;
}

void PermissionManager::Prompt(const QString &token, const QString &kind, const QString &appId,
                               const QString &parentWindow, const QVariantMap &options,
                               const QDBusMessage &message)
{
    if (token.isEmpty() || m_prompts.contains(token)) {
        error(message, QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"),
              QStringLiteral("Prompt token is empty or in use"));
        return;
    }
    if (!PermissionPrompt::supports(kind)) {
        error(message, QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"),
              QStringLiteral("Unknown permission kind"));
        return;
    }
    auto *prompt = new PermissionPrompt(engine(), kind, appId, parentWindow, options, message, this);
    m_prompts.insert(token, prompt);
    connect(prompt, &PermissionPrompt::finished, this, [this, token, prompt] {
        m_prompts.remove(token);
        prompt->deleteLater();
    });
    // A caller that goes away takes its question with it.
    auto *watcher = new QDBusServiceWatcher(message.service(), QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForUnregistration, prompt);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, prompt, &PermissionPrompt::dismiss);
    if (!prompt->show())
        prompt->deleteLater();
}

void PermissionManager::Dismiss(const QString &token, const QDBusMessage &message)
{
    auto *prompt = m_prompts.value(token);
    if (!prompt || prompt->caller() != message.service()) {
        error(message, QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"),
              QStringLiteral("No such prompt for this caller"));
        return;
    }
    prompt->dismiss();
}
