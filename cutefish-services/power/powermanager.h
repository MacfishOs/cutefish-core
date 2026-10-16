#ifndef CUTEFISH_SETTINGS_POWERMANAGER_H
#define CUTEFISH_SETTINGS_POWERMANAGER_H

#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/settings.h"
#include "base/signal.h"

class BrightnessManager;
class DimDisplayAction;
class IdleManager;
class UPowerManager;

class PowerManager
{
public:
    PowerManager(UPowerManager *upowerManager, BrightnessManager *brightness, IdleManager *idle,
                 cutefish::base::Settings &settings, cutefish::base::dbus::Connection *session,
                 cutefish::base::dbus::Connection *system);
    ~PowerManager();

    PowerManager(const PowerManager &) = delete;
    PowerManager &operator=(const PowerManager &) = delete;

    // com.cutefish.Services.Power at /com/cutefish/Services/Power.
    bool publish();

    void setBatteryScreenOff(int timeout);
    void setACScreenOff(int timeout);
    void setACPreventSleep(bool prevent);
    void setDimDisplayTimeout(int timeout);

private:
    void onBatteryChanged();
    void onLidClosedChanged();
    void loadSettings();
    void applyPolicy();
    template<typename T>
    void writePowerSetting(const std::string &key, const T &value);

    cutefish::base::Settings &m_settings;
    cutefish::base::dbus::Connection *m_session;
    UPowerManager *m_upowerManager;
    std::unique_ptr<DimDisplayAction> m_dimDisplayAction;
    std::unique_ptr<cutefish::base::dbus::Object> m_object;
    std::vector<cutefish::base::Subscription> m_subscriptions;

    int m_batteryScreenOff = 300;
    int m_acScreenOff = 1200;
    bool m_acPreventSleep = false;
    bool m_onBattery = false;
};

#endif
