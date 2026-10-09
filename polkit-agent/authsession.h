// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AUTHSESSION_H
#define AUTHSESSION_H

#include <QObject>
#include <QByteArray>
#include <QString>

class QIODevice;
class QLocalSocket;
class QProcess;

// One PAM conversation through polkit's helper: the socket-activated service
// when polkit provides one, else the setuid binary. Deleting the session ends
// the conversation.
class AuthSession : public QObject
{
    Q_OBJECT

public:
    explicit AuthSession(QObject *parent = nullptr);
    ~AuthSession();

    void start(const QString &user, const QString &cookie);
    void respond(const QString &response);

signals:
    void request(const QString &prompt, bool echo);
    void notice(const QString &text, bool error);
    void completed(bool gainedAuthorization);

private:
    bool connectSocket(const QString &user);
    void startProcess(const QString &user);
    void readOutput();
    void processLine(const QByteArray &line);
    void finish(bool gainedAuthorization);

private:
    QLocalSocket *m_socket;
    QProcess *m_process;
    QIODevice *m_channel;
    QByteArray m_buffer;
    bool m_succeeded;
    bool m_finished;
};

#endif // AUTHSESSION_H
