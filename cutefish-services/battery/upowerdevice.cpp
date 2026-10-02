#include "upowerdevice.h"
#include "power.h"

#include <QStringList>
#include <QDBusReply>

UPowerDevice::UPowerDevice(const QString &udi, QObject *parent)
    : QObject(parent)
    , m_device(UP_DBUS_SERVICE,
               udi,
               UP_DBUS_INTERFACE_DEVICE,
               QDBusConnection::systemBus())
    , m_udi(udi)
{
    if (m_device.isValid()) {
        if (m_device.metaObject()->indexOfSignal("Changed()") != -1) {
            connect(&m_device, SIGNAL(Changed()), this, SLOT(slotChanged()));
        } else {
            // for UPower >= 0.99.0, missing Changed() signal
            QDBusConnection::systemBus().connect(UP_DBUS_SERVICE, m_udi, "org.freedesktop.DBus.Properties",
                                                 "PropertiesChanged", this,
                                                 SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
        }

        // UPower 0.99 dropped its Resuming signal; logind reports the wake-up instead.
        QDBusConnection::systemBus().connect("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "PrepareForSleep",
                                             this, SLOT(login1Resuming(bool)));
    }
}

QString UPowerDevice::udi() const
{
    return m_udi;
}

QVariant UPowerDevice::prop(const QString &key) const
{
    checkCache(key);
    return m_cache.value(key);
}

QMap<QString, QVariant> UPowerDevice::allProperties() const
{
    QDBusMessage call = QDBusMessage::createMethodCall(m_device.service(), m_device.path(),
                        "org.freedesktop.DBus.Properties", "GetAll");
    call << m_device.interface();
    QDBusPendingReply< QVariantMap > reply = QDBusConnection::systemBus().asyncCall(call);
    reply.waitForFinished();

    if (reply.isValid()) {
        m_cache = reply.value();
    } else {
        m_cache.clear();
    }

    return m_cache;
}

void UPowerDevice::onPropertiesChanged(const QString &ifaceName, const QVariantMap &changedProps, const QStringList &invalidatedProps)
{
    Q_UNUSED(changedProps);
    Q_UNUSED(invalidatedProps);

    if (ifaceName == UP_DBUS_INTERFACE_DEVICE) {
        slotChanged(); // TODO maybe process the properties separately?
    }
}

void UPowerDevice::login1Resuming(bool active)
{
    if (!active) {
        QDBusReply<void> refreshCall = m_device.asyncCall("Refresh");
        if (refreshCall.isValid()) {
            slotChanged();
        }
    }
}

void UPowerDevice::slotChanged()
{
    m_cache.clear();
    emit changed();
}

void UPowerDevice::checkCache(const QString &key) const
{
    if (m_cache.isEmpty()) { // recreate the cache
        allProperties();
    }

    if (m_cache.contains(key)) {
        return;
    }

    m_cache[key] = m_device.property(key.toUtf8());
}
