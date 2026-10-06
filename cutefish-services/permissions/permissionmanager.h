#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QHash>
#include <QObject>
#include <QVariantMap>
#include <functional>

class PermissionPrompt;
class QQmlEngine;

// com.cutefish.Services.Permissions: shows permission prompts on behalf of
// portals and services, which only speak D-Bus and never draw. One prompt per
// token; the answer is the delayed reply to Prompt.
class PermissionManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.cutefish.Services.Permissions")

public:
    explicit PermissionManager(std::function<QQmlEngine *()> engine, QObject *parent = nullptr);
    ~PermissionManager() override;

public slots:
    // Replies (response, results): 0 allowed, 1 denied or dismissed, 2 failed.
    // results carries whatever the kind's content chose.
    void Prompt(const QString &token, const QString &kind, const QString &appId,
                const QString &parentWindow, const QVariantMap &options,
                const QDBusMessage &message);
    void Dismiss(const QString &token, const QDBusMessage &message);

private:
    std::function<QQmlEngine *()> m_engine;
    QHash<QString, PermissionPrompt *> m_prompts;
};
