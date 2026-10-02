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

    Q_PROPERTY(QString message READ message CONSTANT)
    Q_PROPERTY(QString identity READ identity CONSTANT)

    QString message() { return m_message; }
    QString identity() { return m_identity; }
    QString password() { return m_password; }

signals:
    void failure();

    void cancel();
    void accepted();

private:
    QString m_message;
    QString m_identity;
    QString m_password;

    QQuickView *m_view;
};

#endif // DIALOG_H
