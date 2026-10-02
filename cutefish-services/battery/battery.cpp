#include "battery.h"
#include "batteryadaptor.h"
#include "power.h"
#include "preferences.h"

#include <QDateTime>

enum TimeConstants {
    SecsInDay = 86400,
    SecsInHour = 3600,
    SecsInMinute = 60
};

// ref: https://upower.freedesktop.org/docs/Device.html

Battery::Battery(UPowerDevice *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_pendingCharge(false)
    , m_settings(nullptr)
    , m_lastChargedPercent(0)
    , m_lastChargedSecs(0)
{
    connect(device, SIGNAL(changed()), this, SLOT(slotChanged()));

    updateCache();
    init();
}

void Battery::updateCache()
{
    m_chargeState = chargeState();
    m_pendingCharge = m_device->prop("State").toUInt() == 5;
    m_statusString = statusString();
    m_chargePercent = chargePercent();
    m_capacity = capacity();
    m_timeToEmpty = timeToEmpty();
    m_timeToFull = timeToFull();
    m_voltage = voltage();
}

void Battery::init()
{
    if (type() == Battery::PrimaryBattery) {
        const QString path = QStringLiteral("/com/cutefish/Services/Battery");

        // Another primary battery owns the path; this one must not overwrite its last-charged record.
        if (QDBusConnection::sessionBus().objectRegisteredAt(path))
            return;

        new BatteryAdaptor(this);
        QDBusConnection::sessionBus().registerObject(path, this);

        m_settings = new QSettings(Preferences::file(QStringLiteral("services")), QSettings::IniFormat, this);
        m_settings->beginGroup(QStringLiteral("Battery"));
        m_lastChargedPercent = m_settings->value("LastChargedPercent", 0).toInt();
        m_lastChargedSecs = m_settings->value("LastChargedSecs").toLongLong();
    }
}

void Battery::refresh()
{
    QDBusInterface iface(UP_DBUS_SERVICE, m_device->udi(), UP_DBUS_INTERFACE_DEVICE,
                         QDBusConnection::systemBus());
    if (iface.isValid())
        iface.call("Refresh");
}

Battery::BatteryType Battery::type() const
{
    Battery::BatteryType result = Battery::UnknownBattery;
    const uint t = m_device->prop("Type").toUInt();

    switch (t) {
    case UP_DEVICE_KIND_LINE_POWER: // TODO
        break;
    case UP_DEVICE_KIND_BATTERY:
        result = Battery::PrimaryBattery;
        break;
    case UP_DEVICE_KIND_UPS:
        result = Battery::UpsBattery;
        break;
    case UP_DEVICE_KIND_MONITOR:
        result = Battery::MonitorBattery;
        break;
    case UP_DEVICE_KIND_MOUSE:
        result = Battery::MouseBattery;
        break;
    case UP_DEVICE_KIND_KEYBOARD:
        result = Battery::KeyboardBattery;
        break;
    case UP_DEVICE_KIND_PDA:
        result = Battery::PdaBattery;
        break;
    case UP_DEVICE_KIND_PHONE:
        result = Battery::PhoneBattery;
        break;
    case UP_DEVICE_KIND_GAMING_INPUT:
        result = Battery::GamingInputBattery;
        break;
    case UP_DEVICE_KIND_UNKNOWN: {
        // There is currently no "Bluetooth battery" type, so check if it comes from Bluez
        if (m_device->prop("NativePath").toString().startsWith(QLatin1String("/org/bluez/"))) {
            result = Battery::BluetoothBattery;
        }
        break;
    }
    }

    return result;
}

int Battery::chargePercent() const
{
    return qRound(m_device->prop("Percentage").toDouble());
}

int Battery::capacity() const
{
    return m_device->prop("Capacity").toDouble();
}

Battery::ChargeState Battery::chargeState() const
{
    Battery::ChargeState result = Battery::NoCharge;
    const uint state = m_device->prop("State").toUInt();
    switch (state) {
    case 0:
        result = Battery::NoCharge; // stable or unknown
        break;
    case 1:
        result = Battery::Charging;
        break;
    case 2:
        result = Battery::Discharging;
        break;
    case 3: // TODO "Empty"
        break;
    case 4:
        result = Battery::FullyCharged;
        break;
    case 5: // Pending charge: plugged in, held below full by a charge limit
        result = Battery::NoCharge;
        break;
    case 6: // TODO "Pending discharge"
        break;
    }

    return result;
}

qlonglong Battery::timeToEmpty() const
{
    return m_device->prop("TimeToEmpty").toLongLong();
}

qlonglong Battery::timeToFull() const
{
    return m_device->prop("TimeToFull").toLongLong();
}

double Battery::voltage() const
{
    return m_device->prop("Voltage").toDouble();
}

qlonglong Battery::remainingTime() const
{
    if (chargeState() == Battery::Charging) {
        return timeToFull();
    } else if (chargeState() == Battery::Discharging) {
        return timeToEmpty();
    }

    return -1;
}

QString Battery::formatDuration(qlonglong seconds) const
{
    QString result;
    quint64 secs = seconds;

    int days = secs / SecsInDay;
    secs = secs % SecsInDay;
    int hours = secs / SecsInHour;
    secs = secs % SecsInHour;
    int minutes = secs / SecsInMinute;

    if (days > 0)
        result.push_back(tr("%1d").arg(days));

    if (hours > 0) {
        if (days > 0)
            result.push_back(" ");

        result.push_back(tr("%1h").arg(hours));
    }

    if (minutes > 0) {
        if (hours > 0)
            result.push_back(" ");

        result.push_back(tr("%1m").arg(minutes));
    }

    return result;
}

QString Battery::statusString() const
{
    const Battery::ChargeState state = chargeState();

    if (state == Battery::Charging || state == Battery::Discharging) {
        // UPower reports 0 for several seconds after the power source changes,
        // until it trusts the battery's rate again.
        const qlonglong time = remainingTime();
        if (time < SecsInMinute)
            return tr("Calculating…");

        if (state == Battery::Charging)
            return tr("%1 until fully charged").arg(formatDuration(time));

        return tr("%1 remaining").arg(formatDuration(time));
    } else if (state == Battery::FullyCharged) {
        return tr("Fully charged.");
    } else if (m_device->prop("State").toUInt() == 5) {
        if (m_device->prop("ChargeThresholdEnabled").toBool())
            return tr("Charging paused at %1%").arg(chargePercent());

        return tr("Not charging");
    }

    return QString();
}

int Battery::lastChargedPercent() const
{
    return m_lastChargedPercent;
}

quint64 Battery::lastChargedSecs() const
{
    return m_lastChargedSecs;
}

QString Battery::lastChargedTime() const
{
    QDateTime now = QDateTime::currentDateTime();
    QDateTime time = QDateTime::fromSecsSinceEpoch(m_lastChargedSecs);
    const qint64 secs = time.secsTo(now);

    // formatDuration() drops seconds and wraps negative input; a clock set back lands here too.
    if (secs < SecsInMinute)
        return tr("now");

    return tr("%1 ago").arg(formatDuration(secs));
}

void Battery::slotChanged()
{
    const int old_chargePercent = m_chargePercent;
    const int old_capacity = m_capacity;
    const Battery::ChargeState old_chargeState = m_chargeState;
    const bool old_pendingCharge = m_pendingCharge;
    const QString old_statusString = m_statusString;
    const qlonglong old_timeToEmpty = m_timeToEmpty;
    const qlonglong old_timeToFull = m_timeToFull;
    const double old_voltage = m_voltage;

    updateCache();

    if (old_chargePercent != m_chargePercent) {
        emit chargePercentChanged(m_chargePercent);
    }

    if (old_capacity != m_capacity) {
        emit capacityChanged(m_capacity);
    }

    if (old_chargeState != m_chargeState) {
        emit chargeStateChanged(m_chargeState);
    }

    // Save last charge percentage
    if ((old_chargeState == Battery::Charging || old_chargeState == Battery::FullyCharged
         || old_pendingCharge)
        && m_chargeState == Battery::Discharging) {
        m_lastChargedPercent = m_chargePercent;
        m_lastChargedSecs = QDateTime::currentSecsSinceEpoch();

        if (m_settings) {
            m_settings->setValue("LastChargedPercent", m_lastChargedPercent);
            m_settings->setValue("LastChargedSecs", m_lastChargedSecs);
        }

        emit lastChargedPercentChanged();
        emit lastChargedSecsChanged();
    }

    if (old_voltage != m_voltage) {
        emit voltageChanged(m_voltage);
    }

    // Also the notify signal of statusString, which a state change alone can alter.
    if (old_timeToFull != m_timeToFull || old_timeToEmpty != m_timeToEmpty
        || old_statusString != m_statusString) {
        emit remainingTimeChanged(remainingTime());
    }
}
