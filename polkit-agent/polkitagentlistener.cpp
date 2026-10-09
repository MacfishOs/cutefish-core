// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "polkitagentlistener.h"
#include "authsession.h"
#include "dialog.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QLocale>
#include <QDebug>

#include <pwd.h>
#include <unistd.h>

static const QString login1Service = QStringLiteral("org.freedesktop.login1");
static const QString polkitService = QStringLiteral("org.freedesktop.PolicyKit1");
static const QString cancelledError = QStringLiteral("org.freedesktop.PolicyKit1.Error.Cancelled");
static const QString failedError = QStringLiteral("org.freedesktop.PolicyKit1.Error.Failed");

static const int maxTries = 3;

QDBusArgument &operator<<(QDBusArgument &argument, const PolkitIdentity &identity)
{
    argument.beginStructure();
    argument << identity.kind << identity.details;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PolkitIdentity &identity)
{
    argument.beginStructure();
    argument >> identity.kind >> identity.details;
    argument.endStructure();
    return argument;
}

static QString loginSessionId()
{
    QDBusConnection bus = QDBusConnection::systemBus();

    QDBusMessage sessionCall = QDBusMessage::createMethodCall(login1Service,
                                                              QStringLiteral("/org/freedesktop/login1"),
                                                              QStringLiteral("org.freedesktop.login1.Manager"),
                                                              QStringLiteral("GetSessionByPID"));
    sessionCall << uint(QCoreApplication::applicationPid());

    const QDBusReply<QDBusObjectPath> session = bus.call(sessionCall);

    if (!session.isValid()) {
        qWarning() << "Cannot find the login session:" << session.error().message();
        return QString();
    }

    QDBusMessage idCall = QDBusMessage::createMethodCall(login1Service,
                                                         session.value().path(),
                                                         QStringLiteral("org.freedesktop.DBus.Properties"),
                                                         QStringLiteral("Get"));
    idCall << QStringLiteral("org.freedesktop.login1.Session") << QStringLiteral("Id");

    const QDBusReply<QDBusVariant> id = bus.call(idCall);

    return id.isValid() ? id.value().variant().toString() : QString();
}

static QString userName(uint uid)
{
    const struct passwd *entry = getpwuid(uid);

    return entry ? QString::fromLocal8Bit(entry->pw_name) : QString();
}

// The dialog's own placeholder stands in for the plain password prompt, so it
// stays translated.
static QString promptLabel(const QString &prompt)
{
    QString label = prompt.trimmed();

    while (label.endsWith(QLatin1Char(':')) || label.endsWith(QChar(0xFF1A)))
        label.chop(1);

    label = label.trimmed();

    if (label.compare(QLatin1String("Password"), Qt::CaseInsensitive) == 0)
        return QString();

    return label;
}

PolKitAgentListener::PolKitAgentListener(QObject *parent)
    : QObject(parent)
    , m_hasResponse(false)
    , m_requestPending(false)
    , m_numTries(0)
{
    qDBusRegisterMetaType<PolkitIdentity>();
    qDBusRegisterMetaType<PolkitIdentityList>();
    qDBusRegisterMetaType<PolkitDetails>();
}

bool PolKitAgentListener::registerAgent(const QString &objectPath)
{
    QDBusConnection bus = QDBusConnection::systemBus();

    if (!bus.registerObject(objectPath, this, QDBusConnection::ExportAllSlots)) {
        qWarning() << "Cannot export the authentication agent on the system bus";
        return false;
    }

    const QString sessionId = loginSessionId();

    if (sessionId.isEmpty())
        return false;

    QDBusArgument subject;
    subject.beginStructure();
    subject << QStringLiteral("unix-session") << QVariantMap { { QStringLiteral("session-id"), sessionId } };
    subject.endStructure();

    QDBusMessage call = QDBusMessage::createMethodCall(polkitService,
                                                       QStringLiteral("/org/freedesktop/PolicyKit1/Authority"),
                                                       QStringLiteral("org.freedesktop.PolicyKit1.Authority"),
                                                       QStringLiteral("RegisterAuthenticationAgent"));
    call << QVariant::fromValue(subject)
         << QLocale::system().name() + QStringLiteral(".UTF-8")
         << objectPath;

    const QDBusMessage result = bus.call(call);

    if (result.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "Cannot register the authentication agent:" << result.errorMessage();
        return false;
    }

    return true;
}

void PolKitAgentListener::BeginAuthentication(const QString &actionId,
                                              const QString &message,
                                              const QString &iconName,
                                              const PolkitDetails &details,
                                              const QString &cookie,
                                              const PolkitIdentityList &identities)
{
    Q_UNUSED(actionId);
    Q_UNUSED(iconName);
    Q_UNUSED(details);

    setDelayedReply(true);

    // polkitd lists administrators in no useful order; prefer the one at the
    // keyboard.
    const uint sessionUid = getuid();
    QString user;

    for (const PolkitIdentity &identity : identities) {
        if (identity.kind != QLatin1String("unix-user"))
            continue;

        const uint uid = identity.details.value(QStringLiteral("uid")).toUInt();

        if (user.isEmpty() || uid == sessionUid)
            user = userName(uid);

        if (uid == sessionUid)
            break;
    }

    if (user.isEmpty()) {
        connection().send(QDBusContext::message().createErrorReply(failedError,
                                                                   QStringLiteral("No user to authenticate as")));
        return;
    }

    m_queue.append({ message, cookie, user, QDBusContext::message() });

    startNext();
}

void PolKitAgentListener::CancelAuthentication(const QString &cookie)
{
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i).cookie != cookie)
            continue;

        if (i == 0 && m_dialog) {
            finishCurrent(Withdrawn);
        } else {
            reply(m_queue.takeAt(i), Withdrawn);
        }

        return;
    }
}

void PolKitAgentListener::startNext()
{
    if (m_dialog || m_queue.isEmpty())
        return;

    const Request &request = m_queue.first();

    m_dialog = new Dialog(request.message, request.user);

    connect(m_dialog.data(), &Dialog::accepted, this, &PolKitAgentListener::onDialogAccepted);
    connect(m_dialog.data(), &Dialog::cancel, this, [this] {
        finishCurrent(Dismissed);
    });

    m_dialog->show();

    m_numTries = 0;

    startSession();
}

void PolKitAgentListener::startSession()
{
    const Request &request = m_queue.first();

    m_hasResponse = false;
    m_response.clear();
    m_requestPending = false;

    m_session = new AuthSession(this);

    connect(m_session.data(), &AuthSession::request, this, &PolKitAgentListener::onRequest);
    connect(m_session.data(), &AuthSession::notice, this, &PolKitAgentListener::onNotice);
    connect(m_session.data(), &AuthSession::completed, this, &PolKitAgentListener::onCompleted);

    m_session->start(request.user, request.cookie);
}

void PolKitAgentListener::releaseSession()
{
    if (!m_session)
        return;

    m_session->disconnect(this);
    m_session->deleteLater();
    m_session.clear();
}

void PolKitAgentListener::finishCurrent(Outcome outcome)
{
    releaseSession();

    if (m_dialog) {
        m_dialog->deleteLater();
        m_dialog.clear();
    }

    m_hasResponse = false;
    m_response.clear();
    m_requestPending = false;

    reply(m_queue.takeFirst(), outcome);

    startNext();
}

void PolKitAgentListener::reply(const Request &request, Outcome outcome)
{
    QDBusMessage message;

    switch (outcome) {
    case Completed:
        message = request.call.createReply();
        break;
    case Dismissed:
        message = request.call.createErrorReply(cancelledError,
                                                QStringLiteral("The authentication dialog was dismissed"));
        break;
    case Withdrawn:
        message = request.call.createErrorReply(cancelledError,
                                                QStringLiteral("The request was cancelled"));
        break;
    }

    QDBusConnection::systemBus().send(message);
}

void PolKitAgentListener::onRequest(const QString &prompt, bool echo)
{
    if (m_hasResponse) {
        m_session->respond(m_response);
        m_hasResponse = false;
        m_response.clear();
        return;
    }

    m_requestPending = true;
    m_dialog->setRequest(promptLabel(prompt), echo);
}

void PolKitAgentListener::onNotice(const QString &text, bool error)
{
    m_dialog->setNotice(text, error);
}

void PolKitAgentListener::onCompleted(bool gainedAuthorization)
{
    releaseSession();

    if (gainedAuthorization) {
        finishCurrent(Completed);
        return;
    }

    if (++m_numTries < maxTries) {
        m_dialog->authenticationFailure();
        startSession();
        return;
    }

    finishCurrent(Completed);
}

void PolKitAgentListener::onDialogAccepted()
{
    const QString response = m_dialog->password();

    m_dialog->setNotice(QString(), false);

    if (m_requestPending && m_session) {
        m_requestPending = false;
        m_session->respond(response);
        return;
    }

    m_response = response;
    m_hasResponse = true;
}
