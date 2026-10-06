// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "coredump.h"

#include <QObject>
#include <QString>

class QQmlEngine;
class QQuickWindow;

// One open crash window. Exposed to QML as `crash`. An application dialog
// offers to reopen it; the session restarts its own processes by itself.
class CrashDialog : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool sessionProcess READ sessionProcess CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString icon READ icon CONSTANT)
    Q_PROPERTY(QString fixedFont READ fixedFont CONSTANT)
    Q_PROPERTY(QString reason READ reason NOTIFY dumpChanged)
    Q_PROPERTY(QString report READ report NOTIFY dumpChanged)

public:
    enum class Kind { Application, SessionProcess };

    CrashDialog(QQmlEngine *engine, Kind kind, const QString &id, const QString &name,
                const QString &icon, const QString &fixedFont, QObject *parent = nullptr);
    ~CrashDialog() override;

    bool sessionProcess() const { return m_kind == Kind::SessionProcess; }
    QString name() const { return m_name; }
    QString icon() const { return m_icon; }
    QString fixedFont() const { return m_fixedFont; }
    QString reason() const;
    QString report() const;

    void setDump(const Coredump &dump);
    bool show();

    Q_INVOKABLE void reopen();
    Q_INVOKABLE void close();

Q_SIGNALS:
    void dumpChanged();
    void finished();

private:
    QQmlEngine *m_engine;
    Kind m_kind;
    QString m_id;
    QString m_name;
    QString m_icon;
    QString m_fixedFont;
    Coredump m_dump;
    bool m_hasDump = false;
    bool m_finished = false;
    QQuickWindow *m_window = nullptr;
};
