// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef POLKITAGENTLISTENER_H
#define POLKITAGENTLISTENER_H

#include <QObject>
#include <QString>
#include <QPointer>
#include <QList>
#include <QMap>
#include <QVariantMap>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusArgument>

struct PolkitIdentity
{
    QString kind;
    QVariantMap details;
};

typedef QList<PolkitIdentity> PolkitIdentityList;
typedef QMap<QString, QString> PolkitDetails;

QDBusArgument &operator<<(QDBusArgument &argument, const PolkitIdentity &identity);
const QDBusArgument &operator>>(const QDBusArgument &argument, PolkitIdentity &identity);

Q_DECLARE_METATYPE(PolkitIdentity)

class Dialog;
class AuthSession;
class PolKitAgentListener : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.PolicyKit1.AuthenticationAgent")

public:
    explicit PolKitAgentListener(QObject *parent = nullptr);

    bool registerAgent(const QString &objectPath);

public slots:
    void BeginAuthentication(const QString &actionId,
                             const QString &message,
                             const QString &iconName,
                             const PolkitDetails &details,
                             const QString &cookie,
                             const PolkitIdentityList &identities);
    void CancelAuthentication(const QString &cookie);

private:
    enum Outcome {
        Completed,
        Dismissed,
        Withdrawn,
    };

    struct Request {
        QString message;
        QString cookie;
        QString user;
        QDBusMessage call;
    };

    bool sendRegistration();
    void onPolkitOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void dropRequests();

    void startNext();
    void startSession();
    void releaseSession();
    void finishCurrent(Outcome outcome);
    void reply(const Request &request, Outcome outcome);

    void onRequest(const QString &prompt, bool echo);
    void onNotice(const QString &text, bool error);
    void onCompleted(bool gainedAuthorization);
    void onDialogAccepted();

private:
    QString m_objectPath;

    // polkitd sends requests concurrently; the first one owns the dialog.
    QList<Request> m_queue;

    QPointer<Dialog> m_dialog;
    QPointer<AuthSession> m_session;

    // A response typed before PAM asked for one.
    QString m_response;
    bool m_hasResponse;
    bool m_requestPending;

    int m_numTries;
};

#endif // POLKITAGENTLISTENER_H
