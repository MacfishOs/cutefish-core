#include "powermanager.h"

#include "../battery/upowermanager.h"
#include "dimdisplayaction.h"

#include <algorithm>

using namespace cutefish::base;

static constexpr auto s_powerGroup = "Power/";

static int normalizedTimeout(int timeout)
{
    return timeout < 0 ? -1 : std::max(1, timeout);
}

static int readTimeout(const Settings &settings, const std::string &key, int fallback)
{
    return settings.contains(key) ? normalizedTimeout(int(settings.intValue(key))) : fallback;
}

PowerManager::PowerManager(UPowerManager *upowerManager, BrightnessManager *brightness, IdleManager *idle,
                           Settings &settings, dbus::Connection *session, dbus::Connection *system)
    : m_settings(settings)
    , m_session(session)
    , m_upowerManager(upowerManager)
    , m_dimDisplayAction(std::make_unique<DimDisplayAction>(idle, brightness, session, system))
{
    loadSettings();

    m_onBattery = m_upowerManager && m_upowerManager->onBattery();

    if (m_upowerManager) {
        m_subscriptions.push_back(m_upowerManager->onBatteryChanged.connect([this] {
            onBatteryChanged();
        }));
        m_subscriptions.push_back(m_upowerManager->lidClosedChanged.connect([this] {
            onLidClosedChanged();
        }));

        m_dimDisplayAction->setLidPresent(m_upowerManager->lidIsPresent());
    }

    applyPolicy();

    if (m_upowerManager && m_upowerManager->lidIsClosed())
        m_dimDisplayAction->handleLidClosed();
}

PowerManager::~PowerManager() = default;

bool PowerManager::publish()
{
    if (!m_session)
        return false;
    m_object = std::make_unique<dbus::Object>(m_session, "/com/cutefish/Services/Power",
                                              "com.cutefish.Services.Power");
    m_object->addMethod("setBatteryScreenOff", [this](int32_t value) { setBatteryScreenOff(value); },
                        {{"value"}, {}});
    m_object->addMethod("setACScreenOff", [this](int32_t value) { setACScreenOff(value); }, {{"value"}, {}});
    m_object->addMethod("setACPreventSleep", [this](bool value) { setACPreventSleep(value); }, {{"value"}, {}});
    m_object->addMethod("setDimDisplayTimeout", [this](int32_t value) { setDimDisplayTimeout(value); },
                        {{"value"}, {}});
    return m_object->publish();
}

void PowerManager::loadSettings()
{
    const std::string group = s_powerGroup;
    m_batteryScreenOff = readTimeout(m_settings, group + "BatteryScreenOff", 300);
    m_acScreenOff = readTimeout(m_settings, group + "ACScreenOff", 1200);
    m_acPreventSleep = m_settings.boolValue(group + "ACPreventSleep", false);
}

void PowerManager::applyPolicy()
{
    const int timeout = m_onBattery ? m_batteryScreenOff : m_acScreenOff;
    // A delay of zero keeps the system awake while the display is off.
    const int suspendDelay = m_onBattery ? 300 : (m_acPreventSleep ? 0 : 900);

    m_dimDisplayAction->setPolicy(timeout, suspendDelay);
}

template<typename T>
void PowerManager::writePowerSetting(const std::string &key, const T &value)
{
    m_settings.setValue(s_powerGroup + key, value);
    m_settings.sync();
}

void PowerManager::setBatteryScreenOff(int timeout)
{
    m_batteryScreenOff = normalizedTimeout(timeout);
    writePowerSetting("BatteryScreenOff", m_batteryScreenOff);
    applyPolicy();
}

void PowerManager::setACScreenOff(int timeout)
{
    m_acScreenOff = normalizedTimeout(timeout);
    writePowerSetting("ACScreenOff", m_acScreenOff);
    applyPolicy();
}

void PowerManager::setACPreventSleep(bool prevent)
{
    m_acPreventSleep = prevent;
    writePowerSetting("ACPreventSleep", m_acPreventSleep);
    applyPolicy();
}

void PowerManager::setDimDisplayTimeout(int timeout)
{
    setBatteryScreenOff(timeout);
    setACScreenOff(timeout);
}

void PowerManager::onBatteryChanged()
{
    m_onBattery = m_upowerManager && m_upowerManager->onBattery();
    applyPolicy();
}

void PowerManager::onLidClosedChanged()
{
    if (!m_upowerManager)
        return;

    if (m_upowerManager->lidIsClosed())
        m_dimDisplayAction->handleLidClosed();
    else
        m_dimDisplayAction->handleLidOpened();
}
