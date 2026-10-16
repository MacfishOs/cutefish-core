#include "battery.h"
#include "power.h"

#include <chrono>
#include <cmath>
#include <format>

#include "base/strings.h"

using namespace cutefish::base;
using namespace cutefish::base::strings;

enum TimeConstants {
    SecsInDay = 86400,
    SecsInHour = 3600,
    SecsInMinute = 60
};

static int64_t currentSecsSinceEpoch()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
}

static uint32_t toUInt(const dbus::Variant &value)
{
    return value.get<uint32_t>().value_or(0);
}

static double toDouble(const dbus::Variant &value)
{
    return value.get<double>().value_or(0);
}

static int64_t toLongLong(const dbus::Variant &value)
{
    return value.get<int64_t>().value_or(0);
}

// ref: https://upower.freedesktop.org/docs/Device.html

Battery::Battery(std::unique_ptr<UPowerDevice> device, Settings &settings)
    : m_device(std::move(device))
    , m_settings(settings)
{
    m_changed = m_device->changed.connect([this] {
        slotChanged();
    });

    updateCache();
}

Battery::~Battery() = default;

void Battery::updateCache()
{
    m_chargeState = chargeState();
    m_pendingCharge = state() == 5;
    m_statusString = statusString();
    m_chargePercent = chargePercent();
    m_capacity = capacity();
    m_timeToEmpty = timeToEmpty();
    m_timeToFull = timeToFull();
    m_voltage = voltage();
}

bool Battery::publish(dbus::Connection *bus)
{
    if (type() != Battery::PrimaryBattery || !bus)
        return false;

    m_object = std::make_unique<dbus::Object>(bus, "/com/cutefish/Services/Battery",
                                              "com.cutefish.Services.Battery");
    dbus::Object &object = *m_object;
    object.addMethod("refresh", [this] { refresh(); });
    object.addProperty("chargeState", [this] { return int32_t(m_chargeState); });
    object.addProperty("chargePercent", [this] { return int32_t(m_chargePercent); });
    object.addProperty("capacity", [this] { return int32_t(m_capacity); });
    object.addProperty("voltage", [this] { return m_voltage; });
    object.addProperty("remainingTime", [this] { return remainingTime(); });
    object.addProperty("statusString", [this] { return statusString(); });
    object.addProperty("lastChargedPercent", [this] { return int32_t(m_lastChargedPercent); });
    object.addProperty("lastChargedSecs", [this] { return int64_t(m_lastChargedSecs); });
    object.addProperty("lastChargedTime", [this] { return lastChargedTime(); });
    object.addProperty("udi", [this] { return udi(); });
    object.addSignal("chargeStateChanged", "i", {"value"});
    object.addSignal("chargePercentChanged", "i", {"value"});
    object.addSignal("capacityChanged", "i", {"value"});
    object.addSignal("voltageChanged", "d", {"value"});
    object.addSignal("remainingTimeChanged", "x", {"value"});
    object.addSignal("lastChargedPercentChanged", "");
    object.addSignal("lastChargedSecsChanged", "");
    if (!object.publish()) {
        m_object.reset();
        return false;
    }

    m_published = true;
    m_lastChargedPercent = int(m_settings.intValue("Battery/LastChargedPercent", 0));
    m_lastChargedSecs = uint64_t(m_settings.intValue("Battery/LastChargedSecs", 0));
    return true;
}

void Battery::refresh()
{
    m_device->refresh();
}

uint32_t Battery::state() const
{
    return toUInt(m_device->prop("State"));
}

Battery::BatteryType Battery::type() const
{
    Battery::BatteryType result = Battery::UnknownBattery;
    const uint32_t t = toUInt(m_device->prop("Type"));

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
        if (startsWith(m_device->prop("NativePath").value<std::string>(), "/org/bluez/"))
            result = Battery::BluetoothBattery;
        break;
    }
    }

    return result;
}

int Battery::chargePercent() const
{
    return int(std::lround(toDouble(m_device->prop("Percentage"))));
}

int Battery::capacity() const
{
    return int(toDouble(m_device->prop("Capacity")));
}

Battery::ChargeState Battery::chargeState() const
{
    Battery::ChargeState result = Battery::NoCharge;
    switch (state()) {
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

int64_t Battery::timeToEmpty() const
{
    return toLongLong(m_device->prop("TimeToEmpty"));
}

int64_t Battery::timeToFull() const
{
    return toLongLong(m_device->prop("TimeToFull"));
}

double Battery::voltage() const
{
    return toDouble(m_device->prop("Voltage"));
}

int64_t Battery::remainingTime() const
{
    if (chargeState() == Battery::Charging) {
        return timeToFull();
    } else if (chargeState() == Battery::Discharging) {
        return timeToEmpty();
    }

    return -1;
}

std::string Battery::formatDuration(int64_t seconds)
{
    std::string result;
    uint64_t secs = uint64_t(seconds);

    const uint64_t days = secs / SecsInDay;
    secs = secs % SecsInDay;
    const uint64_t hours = secs / SecsInHour;
    secs = secs % SecsInHour;
    const uint64_t minutes = secs / SecsInMinute;

    if (days > 0)
        result += std::format("{}d", days);

    if (hours > 0) {
        if (days > 0)
            result += ' ';

        result += std::format("{}h", hours);
    }

    if (minutes > 0) {
        if (hours > 0)
            result += ' ';

        result += std::format("{}m", minutes);
    }

    return result;
}

std::string Battery::statusString() const
{
    const Battery::ChargeState chargeState = this->chargeState();

    if (chargeState == Battery::Charging || chargeState == Battery::Discharging) {
        // UPower reports 0 for several seconds after the power source changes,
        // until it trusts the battery's rate again.
        const int64_t time = remainingTime();
        if (time < SecsInMinute)
            return "Calculating…";

        if (chargeState == Battery::Charging)
            return std::format("{} until fully charged", formatDuration(time));

        return std::format("{} remaining", formatDuration(time));
    } else if (chargeState == Battery::FullyCharged) {
        return "Fully charged.";
    } else if (state() == 5) {
        if (m_device->prop("ChargeThresholdEnabled").value<bool>())
            return std::format("Charging paused at {}%", chargePercent());

        return "Not charging";
    }

    return {};
}

int Battery::lastChargedPercent() const
{
    return m_lastChargedPercent;
}

uint64_t Battery::lastChargedSecs() const
{
    return m_lastChargedSecs;
}

std::string Battery::lastChargedTime() const
{
    const int64_t secs = currentSecsSinceEpoch() - int64_t(m_lastChargedSecs);

    // formatDuration() drops seconds and wraps negative input; a clock set back lands here too.
    if (secs < SecsInMinute)
        return "now";

    return std::format("{} ago", formatDuration(secs));
}

void Battery::slotChanged()
{
    const int old_chargePercent = m_chargePercent;
    const int old_capacity = m_capacity;
    const Battery::ChargeState old_chargeState = m_chargeState;
    const bool old_pendingCharge = m_pendingCharge;
    const std::string old_statusString = m_statusString;
    const int64_t old_timeToEmpty = m_timeToEmpty;
    const int64_t old_timeToFull = m_timeToFull;
    const double old_voltage = m_voltage;

    updateCache();

    if (!m_object)
        return;

    if (old_chargePercent != m_chargePercent)
        m_object->emitSignal("chargePercentChanged", int32_t(m_chargePercent));

    if (old_capacity != m_capacity)
        m_object->emitSignal("capacityChanged", int32_t(m_capacity));

    if (old_chargeState != m_chargeState)
        m_object->emitSignal("chargeStateChanged", int32_t(m_chargeState));

    // Save last charge percentage
    if ((old_chargeState == Battery::Charging || old_chargeState == Battery::FullyCharged
         || old_pendingCharge)
        && m_chargeState == Battery::Discharging) {
        m_lastChargedPercent = m_chargePercent;
        m_lastChargedSecs = uint64_t(currentSecsSinceEpoch());

        if (m_published) {
            m_settings.setValue("Battery/LastChargedPercent", m_lastChargedPercent);
            m_settings.setValue("Battery/LastChargedSecs", m_lastChargedSecs);
            m_settings.sync();
        }

        m_object->emitSignal("lastChargedPercentChanged");
        m_object->emitSignal("lastChargedSecsChanged");
    }

    if (old_voltage != m_voltage)
        m_object->emitSignal("voltageChanged", m_voltage);

    // Also the notify signal of statusString, which a state change alone can alter.
    if (old_timeToFull != m_timeToFull || old_timeToEmpty != m_timeToEmpty
        || old_statusString != m_statusString) {
        m_object->emitSignal("remainingTimeChanged", remainingTime());
    }
}
