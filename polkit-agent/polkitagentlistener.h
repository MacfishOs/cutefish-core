// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef POLKITAGENTLISTENER_H
#define POLKITAGENTLISTENER_H

#include <QObject>
#include <QString>
#include <QPointer>

#include <PolkitQt1/Identity>
#include <PolkitQt1/Details>
#include <PolkitQt1/Agent/Listener>
#include <PolkitQt1/Agent/Session>

class Dialog;
class PolKitAgentListener : public PolkitQt1::Agent::Listener
{
    Q_OBJECT

public:
    explicit PolKitAgentListener(QObject *parent = nullptr);

public slots:
    void initiateAuthentication(const QString &actionId,
                                const QString &message,
                                const QString &iconName,
                                const PolkitQt1::Details &details,
                                const QString &cookie,
                                const PolkitQt1::Identity::List &identities,
                                PolkitQt1::Agent::AsyncResult *result);

    bool initiateAuthenticationFinish() { return true; }
    // polkitd withdrew the request: the dialog and session go with it.
    void cancelAuthentication();

    void completed(bool gainedAuthorization);

private:
    void tryAgain();
    void finishObtainPrivilege(bool gainedAuthorization);

private slots:
    void onDialogAccepted();
    void onDialogCanceled();

private:
    QPointer<Dialog> m_dialog;
    QPointer<PolkitQt1::Agent::Session> m_session;
    PolkitQt1::Agent::AsyncResult *m_result;
    PolkitQt1::Identity m_selectedUser;
    QString m_cookie;

    bool m_inProgress;
    bool m_wasCancelled;

    int m_numTries;
};

#endif // POLKITAGENTLISTENER_H
