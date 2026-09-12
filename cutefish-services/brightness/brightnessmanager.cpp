#include "brightnessmanager.h"
#include "brightnessadaptor.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <algorithm>

namespace {

const QString BacklightDir = QStringLiteral("/sys/class/backlight/");
const QString DrmDir = QStringLiteral("/sys/class/drm/");

QString readSysfs(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromLatin1(file.readAll()).trimmed();
}

// Connected DRM connectors, e.g. "card0-eDP-1".
QStringList connectedConnectors()
{
    QStringList result;
    const QStringList entries = QDir(DrmDir).entryList(
            {QStringLiteral("card*-*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        if (readSysfs(DrmDir + entry + QStringLiteral("/status")) == QLatin1String("connected"))
            result.append(entry);
    }
    return result;
}

bool isInternalConnector(const QString &connector)
{
    const QString type = connector.section(QLatin1Char('-'), 1, 1);
    return type == QLatin1String("eDP") || type == QLatin1String("LVDS")
            || type == QLatin1String("DSI");
}

int typeRank(const QString &type)
{
    if (type == QLatin1String("firmware"))
        return 0;
    if (type == QLatin1String("platform"))
        return 1;
    return 2;
}

} // namespace

BrightnessManager::BrightnessManager(QObject *parent)
    : QObject(parent)
    , m_fileWatcher(new QFileSystemWatcher(this))
    , m_ddcWorker(new DdcWorker)
{
    qRegisterMetaType<DdcDisplay>();
    qRegisterMetaType<QList<DdcDisplay>>();

    new BrightnessAdaptor(this);
    QDBusConnection::sessionBus().registerObject(
            QStringLiteral("/com/cutefish/Services/Brightness"), this);

    m_ddcWorker->moveToThread(&m_ddcThread);
    connect(&m_ddcThread, &QThread::finished, m_ddcWorker, &QObject::deleteLater);
    connect(m_ddcWorker, &DdcWorker::probed, this, &BrightnessManager::handleDdcProbed);
    m_ddcThread.start();

    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this,
            &BrightnessManager::handleFileChanged);

    // Monitors need a moment after hotplug before they answer DDC/CI.
    m_rescanTimer.setSingleShot(true);
    m_rescanTimer.setInterval(1500);
    connect(&m_rescanTimer, &QTimer::timeout, this, &BrightnessManager::rescan);
    QDBusConnection::sessionBus().connect(QString(), QStringLiteral("/com/cutefish/DisplayConfig1"),
                                          QStringLiteral("com.cutefish.DisplayConfig1"),
                                          QStringLiteral("StateChanged"), this,
                                          SLOT(scheduleRescan()));

    rescan();
}

BrightnessManager::~BrightnessManager()
{
    m_ddcThread.quit();
    m_ddcThread.wait();
}

int BrightnessManager::maxBrightness()
{
    return m_displays.isEmpty() ? -1 : m_displays.first().maximum;
}

int BrightnessManager::brightness()
{
    return m_displays.isEmpty() ? -1 : percent(m_displays.first());
}

bool BrightnessManager::brightnessEnabled()
{
    return !m_displays.isEmpty();
}

void BrightnessManager::setValue(int value)
{
    if (!m_displays.isEmpty())
        setDisplayBrightness(m_displays.first().connector, value);
}

QStringList BrightnessManager::displays() const
{
    QStringList result;
    for (const Display &display : m_displays)
        result.append(display.connector);
    return result;
}

int BrightnessManager::displayBrightness(const QString &display) const
{
    const Display *entry = find(display);
    return entry ? percent(*entry) : -1;
}

void BrightnessManager::setDisplayBrightness(const QString &connector, int value)
{
    Display *display = find(connector);
    if (!display)
        return;

    int raw = qRound(qBound(0, value, 100) * display->maximum / 100.0);
    // Raw 0 switches some backlights off entirely.
    if (display->kind == Kind::Backlight)
        raw = qMax(1, raw);
    if (raw == display->raw)
        return;

    const int oldPercent = percent(*display);
    display->raw = raw;

    if (display->kind == Kind::Backlight) {
        QDBusMessage message = QDBusMessage::createMethodCall(
                QStringLiteral("org.freedesktop.login1"),
                QStringLiteral("/org/freedesktop/login1/session/auto"),
                QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("SetBrightness"));
        message << QStringLiteral("backlight") << display->device << uint(raw);
        auto *watcher =
                new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [](QDBusPendingCallWatcher *call) {
                    if (call->isError())
                        qWarning() << "Setting backlight brightness failed:"
                                   << call->error().message();
                    call->deleteLater();
                });
    } else {
        m_ddcWorker->requestValue(display->device, raw);
    }

    const int newPercent = percent(*display);
    if (newPercent != oldPercent) {
        emit displayBrightnessChanged(display->connector, newPercent);
        if (display == &m_displays.first())
            emit brightnessChanged();
    }
}

void BrightnessManager::scheduleRescan()
{
    m_rescanTimer.start();
}

void BrightnessManager::rescan()
{
    ++m_generation;
    m_backlights = scanBacklights();

    const QList<DdcDisplay> candidates = ddcCandidates(m_backlights);

    // Keep known DDC displays until the probe answers so sliders do not flicker.
    QList<Display> displays = m_backlights;
    for (const Display &display : std::as_const(m_displays)) {
        if (display.kind != Kind::Ddc)
            continue;
        for (const DdcDisplay &candidate : candidates) {
            if (candidate.connector == display.connector && candidate.device == display.device)
                displays.append(display);
        }
    }
    publish(displays);

    if (!candidates.isEmpty()) {
        QMetaObject::invokeMethod(m_ddcWorker, "probe", Qt::QueuedConnection,
                                  Q_ARG(quint64, m_generation),
                                  Q_ARG(QList<DdcDisplay>, candidates));
    }
}

QList<BrightnessManager::Display> BrightnessManager::scanBacklights() const
{
    const QStringList connected = connectedConnectors();
    QStringList internal;
    for (const QString &connector : connected) {
        if (isInternalConnector(connector))
            internal.append(connector);
    }

    // Firmware and platform interfaces carry no connector link; they drive the internal panel.
    QHash<QString, QPair<int, Display>> best;
    const QStringList entries =
            QDir(BacklightDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : entries) {
        const QString path = BacklightDir + name;
        Display display;
        display.kind = Kind::Backlight;
        display.device = name;
        display.maximum = readSysfs(path + QStringLiteral("/max_brightness")).toInt();
        if (display.maximum <= 0)
            continue;

        const QString parent =
                QFileInfo(QFileInfo(path + QStringLiteral("/device")).canonicalFilePath())
                        .fileName();
        const bool attached =
                QFileInfo::exists(DrmDir + parent) && parent.startsWith(QLatin1String("card"));
        if (attached)
            display.connector = parent;
        else if (!internal.isEmpty())
            display.connector = internal.first();
        if (display.connector.isEmpty() || !connected.contains(display.connector))
            continue;

        const int rank =
                typeRank(readSysfs(path + QStringLiteral("/type"))) * 2 + (attached ? 0 : 1);
        auto it = best.find(display.connector);
        if (it == best.end() || rank < it->first) {
            readBacklight(display);
            best.insert(display.connector, qMakePair(rank, display));
        }
    }

    QList<Display> result;
    for (const QString &connector : connected) {
        if (best.contains(connector))
            result.append(best.value(connector).second);
    }
    return result;
}

QList<DdcDisplay> BrightnessManager::ddcCandidates(const QList<Display> &backlights) const
{
    QList<DdcDisplay> result;
    const QStringList connected = connectedConnectors();
    for (const QString &connector : connected) {
        if (isInternalConnector(connector))
            continue;
        const bool hasBacklight =
                std::any_of(backlights.cbegin(), backlights.cend(), [&](const Display &display) {
                    return display.connector == connector;
                });
        if (hasBacklight)
            continue;

        // HDMI/DVI/VGA link their DDC adapter; DP exposes an AUX-backed i2c child.
        const QString path = DrmDir + connector;
        QString bus =
                QFileInfo(QFileInfo(path + QStringLiteral("/ddc")).canonicalFilePath()).fileName();
        if (!bus.startsWith(QLatin1String("i2c-"))) {
            const QStringList children = QDir(path).entryList({QStringLiteral("i2c-*")},
                                                              QDir::Dirs | QDir::NoDotAndDotDot);
            bus = children.isEmpty() ? QString() : children.first();
        }
        if (bus.isEmpty() || !QFileInfo::exists(QStringLiteral("/dev/") + bus))
            continue;

        DdcDisplay candidate;
        candidate.connector = connector;
        candidate.device = QStringLiteral("/dev/") + bus;
        result.append(candidate);
    }
    return result;
}

void BrightnessManager::handleDdcProbed(quint64 generation, const QList<DdcDisplay> &probed)
{
    if (generation != m_generation)
        return;

    QList<Display> displays = m_backlights;
    for (const DdcDisplay &entry : probed) {
        Display display;
        display.connector = entry.connector;
        display.kind = Kind::Ddc;
        display.device = entry.device;
        display.maximum = entry.maximum;
        display.raw = entry.value;
        // A slider write may still be queued; its value is newer than the probe.
        if (const Display *known = find(entry.connector); known && known->device == entry.device)
            display.raw = known->raw;
        displays.append(display);
    }
    publish(displays);
}

void BrightnessManager::publish(QList<Display> displays)
{
    const QStringList oldConnectors = this->displays();
    const int oldDefault = brightness();
    const QList<Display> old = m_displays;
    m_displays = std::move(displays);

    if (!m_fileWatcher->files().isEmpty())
        m_fileWatcher->removePaths(m_fileWatcher->files());
    for (const Display &display : std::as_const(m_displays)) {
        if (display.kind == Kind::Backlight)
            m_fileWatcher->addPath(BacklightDir + display.device + QStringLiteral("/brightness"));
    }

    if (this->displays() != oldConnectors)
        emit displaysChanged();

    for (const Display &display : std::as_const(m_displays)) {
        const auto it = std::find_if(old.cbegin(), old.cend(), [&](const Display &entry) {
            return entry.connector == display.connector;
        });
        if (it == old.cend() || percent(*it) != percent(display))
            emit displayBrightnessChanged(display.connector, percent(display));
    }

    if (brightness() != oldDefault)
        emit brightnessChanged();
}

void BrightnessManager::readBacklight(Display &display) const
{
    display.raw = readSysfs(BacklightDir + display.device + QStringLiteral("/brightness")).toInt();
}

BrightnessManager::Display *BrightnessManager::find(const QString &display)
{
    for (Display &entry : m_displays) {
        if (entry.connector == display)
            return &entry;
    }
    return nullptr;
}

const BrightnessManager::Display *BrightnessManager::find(const QString &display) const
{
    return const_cast<BrightnessManager *>(this)->find(display);
}

int BrightnessManager::percent(const Display &display) const
{
    return display.maximum > 0 ? qRound(display.raw * 100.0 / display.maximum) : -1;
}

void BrightnessManager::handleFileChanged(const QString &path)
{
    for (Display &display : m_displays) {
        if (display.kind != Kind::Backlight
            || BacklightDir + display.device + QStringLiteral("/brightness") != path)
            continue;

        // Some writers replace the file, which drops it from the watcher.
        if (!m_fileWatcher->files().contains(path))
            m_fileWatcher->addPath(path);

        const int oldPercent = percent(display);
        readBacklight(display);
        if (percent(display) != oldPercent) {
            emit displayBrightnessChanged(display.connector, percent(display));
            if (&display == &m_displays.first())
                emit brightnessChanged();
        }
        return;
    }
}
