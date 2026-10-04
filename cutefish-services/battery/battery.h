#ifndef BATTERY_H
#define BATTERY_H

#include <QObject>
#include <QSettings>
#include "upowerdevice.h"

class Battery : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int chargePercent READ chargePercent NOTIFY chargePercentChanged)
    Q_PROPERTY(int capacity READ capacity NOTIFY capacityChanged)
    Q_PROPERTY(double voltage READ voltage NOTIFY voltageChanged)
    Q_PROPERTY(int chargeState READ chargeState NOTIFY chargeStateChanged)
    Q_PROPERTY(qlonglong remainingTime READ remainingTime NOTIFY remainingTimeChanged)
    Q_PROPERTY(QString statusString READ statusString NOTIFY remainingTimeChanged)
    Q_PROPERTY(int lastChargedPercent READ lastChargedPercent NOTIFY lastChargedPercentChanged)
    Q_PROPERTY(quint64 lastChargedSecs READ lastChargedSecs NOTIFY lastChargedSecsChanged)
    Q_PROPERTY(QString lastChargedTime READ lastChargedTime NOTIFY lastChargedSecsChanged)
    Q_PROPERTY(QString udi READ udi)

public:
    /**
     * This enum type defines the type of the device holding the battery
     *
     * - PdaBattery : A battery in a Personal Digital Assistant
     * - UpsBattery : A battery in an Uninterruptible Power Supply
     * - PrimaryBattery : A primary battery for the system (for example laptop battery)
     * - MouseBattery : A battery in a mouse
     * - KeyboardBattery : A battery in a keyboard
     * - KeyboardMouseBattery : A battery in a combined keyboard and mouse
     * - CameraBattery : A battery in a camera
     * - PhoneBattery : A battery in a phone
     * - MonitorBattery : A battery in a monitor
     * - GamingInputBattery : A battery in a gaming input device (for example a wireless game pad)
     * - BluetoothBattery: A generic Bluetooth device battery (if its type isn't known, a Bluetooth
     *                     mouse would normally show up as a MouseBattery), @since 5.54
     * - UnknownBattery : A battery in an unknown device
     */
    enum BatteryType { UnknownBattery, PdaBattery, UpsBattery,
                       PrimaryBattery, MouseBattery, KeyboardBattery,
                       KeyboardMouseBattery, CameraBattery,
                       PhoneBattery, MonitorBattery, GamingInputBattery,
                       BluetoothBattery
                     };
    Q_ENUM(BatteryType)

    /**
     * This enum type defines charge state of a battery
     *
     * - NoCharge : Battery charge is stable, not charging or discharging or
     *              the state is Unknown
     * - Charging : Battery is charging
     * - Discharging : Battery is discharging
     * - FullyCharged: The battery is fully charged; a battery not necessarily
     *                 charges up to 100%
     */
    enum ChargeState { NoCharge, Charging, Discharging, FullyCharged };
    Q_ENUM(ChargeState)

    explicit Battery(UPowerDevice *device, QObject *parent = nullptr);

    void updateCache();
    void init();
    void refresh();

    Battery::BatteryType type() const;

    int chargePercent() const;
    int capacity() const;

    Battery::ChargeState chargeState() const;

    // Number of seconds until the power source is considered empty. Is set to 0 if unknown.
    // This property is only valid if the property type has the value "battery".
    qlonglong timeToEmpty() const;
    // Number of seconds until the power source is considered full. Is set to 0 if unknown.
    // This property is only valid if the property type has the value "battery".
    qlonglong timeToFull() const;

    double voltage() const;

    qlonglong remainingTime() const;

    static QString formatDuration(qlonglong seconds);
    QString statusString() const;
    int lastChargedPercent() const;
    quint64 lastChargedSecs() const;

    QString lastChargedTime() const;

    QString udi() const { return m_device->udi(); }

signals:
    void chargePercentChanged(int value);
    void capacityChanged(int value);
    void chargeStateChanged(int newState);
    void voltageChanged(double voltage);
    void remainingTimeChanged(qlonglong time);
    void lastChargedPercentChanged();
    void lastChargedSecsChanged();

private slots:
    void slotChanged();

private:
    UPowerDevice *m_device;

    int m_chargePercent;
    int m_capacity;
    Battery::ChargeState m_chargeState;
    // Plugged in but held from charging, as a charge limit does.
    bool m_pendingCharge;
    QString m_statusString;
    qlonglong m_timeToEmpty;
    qlonglong m_timeToFull;
    double m_voltage;

    QSettings *m_settings;
    int m_lastChargedPercent;
    quint64 m_lastChargedSecs;
};

#endif // BATTERY_H
