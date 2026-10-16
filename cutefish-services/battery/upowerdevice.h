#ifndef UPOWERDEVICE_H
#define UPOWERDEVICE_H

#include <string>

#include "base/dbus.h"
#include "base/signal.h"

class UPowerDevice
{
public:
    UPowerDevice(cutefish::base::dbus::Connection *bus, const std::string &udi);
    ~UPowerDevice();

    UPowerDevice(const UPowerDevice &) = delete;
    UPowerDevice &operator=(const UPowerDevice &) = delete;

    const std::string &udi() const { return m_udi; }

    cutefish::base::dbus::Variant prop(const std::string &key) const;
    const cutefish::base::dbus::VariantMap &allProperties() const;

    void refresh() const;

    cutefish::base::Signal<> changed;

private:
    void slotChanged();

    cutefish::base::dbus::Proxy m_device;
    cutefish::base::dbus::Match m_propertiesChanged;
    cutefish::base::dbus::Match m_prepareForSleep;
    mutable cutefish::base::dbus::VariantMap m_cache;
    mutable bool m_cached = false;
    std::string m_udi;
};

#endif // UPOWERDEVICE_H
