#include "upowerdevice.h"
#include "power.h"

using namespace cutefish::base;

UPowerDevice::UPowerDevice(dbus::Connection *bus, const std::string &udi)
    : m_device(bus, UP_DBUS_SERVICE, udi, UP_DBUS_INTERFACE_DEVICE)
    , m_udi(udi)
{
    if (!bus)
        return;

    m_propertiesChanged = m_device.onPropertiesChanged(
            [this](const dbus::VariantMap &, const std::vector<std::string> &) {
                slotChanged();
            });

    // UPower 0.99 dropped its Resuming signal; logind reports the wake-up instead.
    const dbus::Proxy logind(bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                             "org.freedesktop.login1.Manager");
    m_prepareForSleep = logind.onSignal("PrepareForSleep", [this](const dbus::Message &message) {
        if (message.argument<bool>().value_or(true))
            return;
        refresh();
        slotChanged();
    });
}

UPowerDevice::~UPowerDevice() = default;

dbus::Variant UPowerDevice::prop(const std::string &key) const
{
    const dbus::VariantMap &properties = allProperties();
    const auto it = properties.find(key);
    return it == properties.end() ? dbus::Variant() : it->second;
}

const dbus::VariantMap &UPowerDevice::allProperties() const
{
    if (!m_cached) {
        m_cache = m_device.getAll().value<dbus::VariantMap>().value_or(dbus::VariantMap());
        m_cached = true;
    }
    return m_cache;
}

void UPowerDevice::refresh() const
{
    m_device.send("Refresh");
}

void UPowerDevice::slotChanged()
{
    m_cached = false;
    m_cache.clear();
    changed.emit();
}
