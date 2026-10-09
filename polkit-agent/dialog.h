// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DIALOG_H
#define DIALOG_H

#include <QObject>
#include <QString>
#include <QQuickView>

class Dialog : public QObject
{
    Q_OBJECT

public:
    explicit Dialog(const QString &message, const QString &identity);
    ~Dialog();

    Q_INVOKABLE void setConfirmationResult(const QString &password = QString());
    Q_INVOKABLE void rejected();
    Q_INVOKABLE void show();

    Q_INVOKABLE void authenticationFailure();

    void setRequest(const QString &prompt, bool echo);
    void setNotice(const QString &text, bool error);

    Q_PROPERTY(QString message READ message CONSTANT)
    Q_PROPERTY(QString identity READ identity CONSTANT)
    Q_PROPERTY(QString prompt READ prompt NOTIFY requestChanged)
    Q_PROPERTY(bool echo READ echo NOTIFY requestChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(bool noticeIsError READ noticeIsError NOTIFY noticeChanged)

    QString message() { return m_message; }
    QString identity() { return m_identity; }
    QString password() { return m_password; }
    QString prompt() { return m_prompt; }
    bool echo() { return m_echo; }
    QString notice() { return m_notice; }
    bool noticeIsError() { return m_noticeIsError; }

signals:
    void failure();
    void requested();
    void requestChanged();
    void noticeChanged();

    void cancel();
    void accepted();

private:
    QString m_message;
    QString m_identity;
    QString m_password;
    QString m_prompt;
    bool m_echo;
    QString m_notice;
    bool m_noticeIsError;

    QQuickView *m_view;
};

#endif // DIALOG_H
