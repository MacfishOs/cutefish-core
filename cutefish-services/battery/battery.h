#ifndef BATTERY_H
#define BATTERY_H

#include <cstdint>
#include <memory>
#include <string>

#include "base/dbus.h"
#include "base/settings.h"
#include "base/signal.h"
#include "upowerdevice.h"

class Battery
{
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

    Battery(std::unique_ptr<UPowerDevice> device, cutefish::base::Settings &settings);
    ~Battery();

    Battery(const Battery &) = delete;
    Battery &operator=(const Battery &) = delete;

    // com.cutefish.Services.Battery at /com/cutefish/Services/Battery, for the
    // primary battery that holds the path.
    bool publish(cutefish::base::dbus::Connection *bus);

    void updateCache();
    void refresh();

    Battery::BatteryType type() const;

    int chargePercent() const;
    int capacity() const;

    Battery::ChargeState chargeState() const;

    // Number of seconds until the power source is considered empty. Is set to 0 if unknown.
    // This property is only valid if the property type has the value "battery".
    int64_t timeToEmpty() const;
    // Number of seconds until the power source is considered full. Is set to 0 if unknown.
    // This property is only valid if the property type has the value "battery".
    int64_t timeToFull() const;

    double voltage() const;

    int64_t remainingTime() const;

    static std::string formatDuration(int64_t seconds);
    std::string statusString() const;
    int lastChargedPercent() const;
    uint64_t lastChargedSecs() const;

    std::string lastChargedTime() const;

    const std::string &udi() const { return m_device->udi(); }

private:
    void slotChanged();
    uint32_t state() const;

    std::unique_ptr<UPowerDevice> m_device;
    cutefish::base::Settings &m_settings;
    cutefish::base::Subscription m_changed;
    std::unique_ptr<cutefish::base::dbus::Object> m_object;

    int m_chargePercent = 0;
    int m_capacity = 0;
    Battery::ChargeState m_chargeState = NoCharge;
    // Plugged in but held from charging, as a charge limit does.
    bool m_pendingCharge = false;
    std::string m_statusString;
    int64_t m_timeToEmpty = 0;
    int64_t m_timeToFull = 0;
    double m_voltage = 0;

    // The last charge is kept only by the battery that holds the D-Bus path.
    bool m_published = false;
    int m_lastChargedPercent = 0;
    uint64_t m_lastChargedSecs = 0;
};

#endif // BATTERY_H
