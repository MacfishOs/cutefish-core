#include "applicationscope.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDebug>
#include <QList>

namespace {

// (sv) and (sa(sv)): unit properties and auxiliary units.
struct UnitProperty {
    QString name;
    QDBusVariant value;
};

struct AuxiliaryUnit {
    QString name;
    QList<UnitProperty> properties;
};

QDBusArgument &operator<<(QDBusArgument &arg, const UnitProperty &value)
{
    arg.beginStructure();
    arg << value.name << value.value;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, UnitProperty &value)
{
    arg.beginStructure();
    arg >> value.name >> value.value;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const AuxiliaryUnit &value)
{
    arg.beginStructure();
    arg << value.name << value.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, AuxiliaryUnit &value)
{
    arg.beginStructure();
    arg >> value.name >> value.properties;
    arg.endStructure();
    return arg;
}

} // namespace

static void registerTypes()
{
    static const bool registered = [] {
        qDBusRegisterMetaType<UnitProperty>();
        qDBusRegisterMetaType<QList<UnitProperty>>();
        qDBusRegisterMetaType<AuxiliaryUnit>();
        qDBusRegisterMetaType<QList<AuxiliaryUnit>>();
        return true;
    }();
    Q_UNUSED(registered)
}

// systemd's unit name escaping: [a-zA-Z0-9:_.] pass, a leading '.' and
// everything else become \xNN of the UTF-8 byte.
static QString escape(const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    QString result;
    for (int i = 0; i < bytes.size(); ++i) {
        const char c = bytes.at(i);
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == ':' || c == '_' || (c == '.' && i > 0);
        if (plain)
            result.append(QLatin1Char(c));
        else
            result.append(QStringLiteral("\\x%1").arg(uchar(c), 2, 16, QLatin1Char('0')));
    }
    return result;
}

Q_DECLARE_METATYPE(UnitProperty)
Q_DECLARE_METATYPE(AuxiliaryUnit)

QString ApplicationScope::unitName(const QString &appId, uint pid)
{
    return QStringLiteral("app-cutefish-%1-%2.scope").arg(escape(appId)).arg(pid);
}

void ApplicationScope::adopt(const QString &appId, uint pid)
{
    if (appId.isEmpty() || pid == 0)
        return;
    registerTypes();
    QList<UnitProperty> properties;
    properties.append({QStringLiteral("PIDs"),
                       QDBusVariant(QVariant::fromValue(QList<uint> {pid}))});
    properties.append({QStringLiteral("CollectMode"),
                       QDBusVariant(QStringLiteral("inactive-or-failed"))});
    auto call = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
            QStringLiteral("org.freedesktop.systemd1.Manager"), QStringLiteral("StartTransientUnit"));
    call.setArguments({unitName(appId, pid), QStringLiteral("fail"),
                       QVariant::fromValue(properties), QVariant::fromValue(QList<AuxiliaryUnit>())});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call));
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
                     [appId, pid](QDBusPendingCallWatcher *watcher) {
                         const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
                         if (reply.isError())
                             qWarning() << "AppRuntime: no systemd scope for" << appId << pid
                                        << reply.error().message();
                         watcher->deleteLater();
                     });
}
