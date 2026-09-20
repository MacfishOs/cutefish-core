#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QQmlEngine;
class QQuickWindow;

// One open prompt window. Exposed to QML as `prompt`; the content component
// for the kind supplies the wording and the choice details.
class PermissionPrompt : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString kind MEMBER m_kind CONSTANT)
    Q_PROPERTY(QString content MEMBER m_content CONSTANT)
    Q_PROPERTY(QString icon MEMBER m_icon CONSTANT)
    Q_PROPERTY(QJsonObject application MEMBER m_application CONSTANT)
    Q_PROPERTY(QJsonObject request MEMBER m_request CONSTANT)
    Q_PROPERTY(bool canAlwaysAllow MEMBER m_canAlwaysAllow CONSTANT)

public:
    // Known kinds map to a content component; an unknown kind is refused.
    static bool supports(const QString &kind);

    PermissionPrompt(QQmlEngine *engine, const QString &kind, const QString &appId,
                     const QString &parentWindow, const QVariantMap &options,
                     const QDBusMessage &message, QObject *parent = nullptr);
    ~PermissionPrompt() override;

    QString caller() const { return m_message.service(); }
    bool show();
    void dismiss();

    Q_INVOKABLE void allow(const QJsonObject &details, bool always);
    Q_INVOKABLE void deny();
    // The icon name of an application's desktop entry, for rows that stand
    // for another application's windows.
    Q_INVOKABLE QString applicationIcon(const QString &appId) const;

Q_SIGNALS:
    void finished();

private:
    void reply(uint response, const QVariantMap &results);

    QQmlEngine *m_engine;
    QDBusMessage m_message;
    QString m_kind;
    QString m_content;
    QString m_icon;
    QString m_parentWindow;
    QJsonObject m_application;
    QJsonObject m_request;
    bool m_canAlwaysAllow = false;
    bool m_finished = false;
    QQuickWindow *m_window = nullptr;
};
