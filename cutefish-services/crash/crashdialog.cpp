// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "crashdialog.h"
#include "applicationruntime.h"

#include <QDebug>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>

CrashDialog::CrashDialog(QQmlEngine *engine, Kind kind, const QString &id, const QString &name,
                         const QString &icon, const QString &fixedFont, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_kind(kind)
    , m_id(id)
    , m_name(name)
    , m_icon(icon)
    , m_fixedFont(fixedFont)
{
}

CrashDialog::~CrashDialog()
{
    delete m_window;
}

QString CrashDialog::reason() const
{
    return m_hasDump ? m_dump.signalDescription() : QString();
}

QString CrashDialog::report() const
{
    return m_hasDump && !m_dump.threads.isEmpty() ? m_dump.report(m_name) : QString();
}

void CrashDialog::setDump(const Coredump &dump)
{
    m_dump = dump;
    m_hasDump = true;
    Q_EMIT dumpChanged();
}

bool CrashDialog::show()
{
    QQmlComponent component(m_engine, QUrl(QStringLiteral("qrc:/crash/CrashDialog.qml")));
    auto *context = new QQmlContext(m_engine->rootContext(), this);
    context->setContextProperty(QStringLiteral("crash"), this);
    m_window = qobject_cast<QQuickWindow *>(component.create(context));
    if (!m_window) {
        qWarning() << "Cannot create the crash dialog:" << component.errorString();
        return false;
    }
    connect(m_window, &QQuickWindow::closing, this, &CrashDialog::close);
    m_window->show();
    m_window->requestActivate();
    return true;
}

void CrashDialog::reopen()
{
    if (m_kind == Kind::Application)
        ApplicationRuntime::instance()->launchApplication(m_id);
    close();
}

void CrashDialog::close()
{
    if (m_finished)
        return;
    m_finished = true;
    if (m_window)
        m_window->hide();
    Q_EMIT finished();
}
