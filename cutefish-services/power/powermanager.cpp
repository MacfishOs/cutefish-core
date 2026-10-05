#include "powermanager.h"

#include "../battery/upowermanager.h"
#include "dimdisplayaction.h"
#include "preferences.h"

static constexpr auto s_powerGroup = "Power";

static int normalizedTimeout(int timeout)
{
    return timeout < 0 ? -1 : qMax(1, timeout);
}

static int readTimeout(const QSettings &settings, const QString &key, int fallback)
{
    return settings.contains(key) ? normalizedTimeout(settings.value(key).toInt()) : fallback;
}

PowerManager::PowerManager(UPowerManager *upowerManager, QObject *parent)
    : QObject(parent)
    , m_settings(Preferences::file(QStringLiteral("services")), QSettings::IniFormat)
    , m_upowerManager(upowerManager)
    , m_dimDisplayAction(new DimDisplayAction(this))
{
    loadSettings();

    m_onBattery = m_upowerManager && m_upowerManager->onBattery();

    if (m_upowerManager) {
        connect(m_upowerManager, &UPowerManager::onBatteryChanged, this,
                &PowerManager::onBatteryChanged);
        connect(m_upowerManager, &UPowerManager::lidClosedChanged, this,
                &PowerManager::onLidClosedChanged);

        m_dimDisplayAction->setLidPresent(m_upowerManager->lidIsPresent());
    }

    applyPolicy();

    if (m_upowerManager && m_upowerManager->lidIsClosed())
        m_dimDisplayAction->handleLidClosed();
}

void PowerManager::loadSettings()
{
    m_settings.beginGroup(QLatin1String(s_powerGroup));
    m_batteryScreenOff = readTimeout(m_settings, QStringLiteral("BatteryScreenOff"), 300);
    m_acScreenOff = readTimeout(m_settings, QStringLiteral("ACScreenOff"), 1200);
    m_acPreventSleep = m_settings.value(QStringLiteral("ACPreventSleep"), false).toBool();
    m_settings.endGroup();
}

void PowerManager::applyPolicy()
{
    const int timeout = m_onBattery ? m_batteryScreenOff : m_acScreenOff;
    // A delay of zero keeps the system awake while the display is off.
    const int suspendDelay = m_onBattery ? 300 : (m_acPreventSleep ? 0 : 900);

    m_dimDisplayAction->setPolicy(timeout, suspendDelay);
}

void PowerManager::writePowerSetting(const QString &key, const QVariant &value)
{
    m_settings.beginGroup(QLatin1String(s_powerGroup));
    m_settings.setValue(key, value);
    m_settings.endGroup();
    m_settings.sync();
}

void PowerManager::setBatteryScreenOff(int timeout)
{
    m_batteryScreenOff = normalizedTimeout(timeout);
    writePowerSetting(QStringLiteral("BatteryScreenOff"), m_batteryScreenOff);
    applyPolicy();
}

void PowerManager::setACScreenOff(int timeout)
{
    m_acScreenOff = normalizedTimeout(timeout);
    writePowerSetting(QStringLiteral("ACScreenOff"), m_acScreenOff);
    applyPolicy();
}

void PowerManager::setACPreventSleep(bool prevent)
{
    m_acPreventSleep = prevent;
    writePowerSetting(QStringLiteral("ACPreventSleep"), m_acPreventSleep);
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
