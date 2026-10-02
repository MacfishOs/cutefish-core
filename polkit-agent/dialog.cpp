// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dialog.h"

#include <windowhints.h>

#include <QGuiApplication>
#include <QQmlContext>
#include <QScreen>
#include <QDebug>

Dialog::Dialog(const QString &message, const QString &identity)
    : m_message(message)
    , m_identity(identity)
    , m_password(QString())
    , m_view(new QQuickView)
{
    qDebug() << "Creating ConfirmationDialog";

    Cutefish::WindowHints::forWindow(m_view)->setSkipTaskbar(true);
    m_view->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_view->rootContext()->setContextProperty("confirmation", this);
    m_view->rootContext()->setContextProperty("rootWindow", m_view);
    m_view->setResizeMode(QQuickView::SizeViewToRootObject);
    m_view->setDefaultAlphaBuffer(true);
    m_view->setColor(Qt::transparent);
    m_view->setSource(QUrl(QStringLiteral("qrc:/main.qml")));
    m_view->setVisible(false);
}

Dialog::~Dialog()
{
    qDebug() << "Deleting ConfirmationDialog";

    delete m_view;
}

void Dialog::setConfirmationResult(const QString &passwd)
{
    m_password = passwd;

    emit accepted();
}

void Dialog::rejected()
{
    emit cancel();
}

void Dialog::show()
{
    m_view->show();
    m_view->setScreen(qGuiApp->primaryScreen());
    m_view->setX((m_view->screen()->geometry().width() - m_view->geometry().width()) / 2);
    m_view->setY((m_view->screen()->geometry().height() - m_view->geometry().height()) / 2);
}

void Dialog::authenticationFailure()
{
    emit failure();
}
