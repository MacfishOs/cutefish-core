// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "privacyservice.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QDir>
#include <QFile>

static const QString kAction = QStringLiteral("com.cutefish.privacy.switch-devices");
static const char *const kCamera = "camera";
static const char *const kMicrophone = "microphone";
static const int kIdleTimeout = 60 * 1000;

// polkit's Subject: a kind and its details.
struct PolkitSubject
{
    QString kind;
    QVariantMap details;
};

Q_DECLARE_METATYPE(PolkitSubject)

static QDBusArgument &operator<<(QDBusArgument &argument, const PolkitSubject &subject)
{
    argument.beginStructure();
    argument << subject.kind << subject.details;
    argument.endStructure();
    return argument;
}

static const QDBusArgument &operator>>(const QDBusArgument &argument, PolkitSubject &subject)
{
    argument.beginStructure();
    argument >> subject.kind >> subject.details;
    argument.endStructure();
    return argument;
}

PrivacyService::PrivacyService(const QString &stateDirectory, QObject *parent)
    : QObject(parent)
    , m_stateDirectory(stateDirectory)
{
    qDBusRegisterMetaType<PolkitSubject>();
    qDBusRegisterMetaType<QMap<QString, QString>>();

    QDir().mkpath(m_stateDirectory);

    // Again at start, for devices that were present before the udev rules.
    if (!cameraEnabled())
        m_cameras.setAuthorized(false);

    if (!microphoneEnabled())
        m_captureSwitches.setLocked(true);

    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(kIdleTimeout);
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        if (!m_captureSwitches.isLocked())
            Q_EMIT idle();
    });
    m_idleTimer.start();
}

bool PrivacyService::cameraEnabled() const
{
    return !QFile::exists(flagPath(kCamera));
}

bool PrivacyService::microphoneEnabled() const
{
    return !QFile::exists(flagPath(kMicrophone));
}

void PrivacyService::setCameraEnabled(bool enabled)
{
    authorize([this, enabled] {
        applyCamera(enabled);
    });
}

void PrivacyService::setMicrophoneEnabled(bool enabled)
{
    authorize([this, enabled] {
        applyMicrophone(enabled);
    });
}

void PrivacyService::authorize(std::function<void()> apply)
{
    m_idleTimer.start();

    if (!calledFromDBus()) {
        apply();
        return;
    }

    setDelayedReply(true);
    const QDBusMessage request = message();

    QDBusMessage check = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.PolicyKit1"),
                                                        QStringLiteral("/org/freedesktop/PolicyKit1/Authority"),
                                                        QStringLiteral("org.freedesktop.PolicyKit1.Authority"),
                                                        QStringLiteral("CheckAuthorization"));
    const PolkitSubject subject {QStringLiteral("system-bus-name"),
                                 {{QStringLiteral("name"), request.service()}}};
    // 1: AllowUserInteraction, should an administrator require a password.
    check << QVariant::fromValue(subject) << kAction << QVariant::fromValue(QMap<QString, QString>())
          << uint(1) << QString();

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(check), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [request, apply](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();

        bool authorized = false;
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            const QDBusArgument result = reply.arguments().constFirst().value<QDBusArgument>();
            result.beginStructure();
            result >> authorized;
        } else {
            qWarning() << "cutefish-privacy: polkit check failed:" << reply.errorMessage();
        }

        if (!authorized) {
            QDBusConnection::systemBus().send(request.createErrorReply(
                QDBusError::AccessDenied, QStringLiteral("Not authorized to turn the camera or microphone on or off")));
            return;
        }

        apply();
        QDBusConnection::systemBus().send(request.createReply());
    });
}

void PrivacyService::applyCamera(bool enabled)
{
    const bool changed = enabled != cameraEnabled();

    // The flag before the interfaces, so a camera plugged in meanwhile follows it.
    setFlag(kCamera, !enabled);
    m_cameras.setAuthorized(enabled);

    if (changed)
        Q_EMIT cameraEnabledChanged(enabled);
}

void PrivacyService::applyMicrophone(bool enabled)
{
    const bool changed = enabled != microphoneEnabled();

    setFlag(kMicrophone, !enabled);
    m_captureSwitches.setLocked(!enabled);

    if (changed)
        Q_EMIT microphoneEnabledChanged(enabled);
}

QString PrivacyService::flagPath(const char *device) const
{
    return m_stateDirectory + QLatin1Char('/') + QLatin1String(device) + QStringLiteral("-off");
}

void PrivacyService::setFlag(const char *device, bool off)
{
    QFile flag(flagPath(device));

    if (!off) {
        flag.remove();
        return;
    }

    if (!flag.open(QIODevice::WriteOnly))
        qWarning() << "cutefish-privacy: cannot write" << flag.fileName() << flag.errorString();
}
