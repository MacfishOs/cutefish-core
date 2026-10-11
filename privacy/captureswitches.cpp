// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "captureswitches.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSocketNotifier>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <sound/asound.h>

#include <vector>

static QList<snd_ctl_elem_id> captureSwitches(int fd)
{
    snd_ctl_elem_list list = {};
    if (::ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) < 0)
        return {};

    std::vector<snd_ctl_elem_id> ids(list.count);
    list.space = list.count;
    list.pids = ids.data();
    if (::ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) < 0)
        return {};

    // ALSA names every capture mute "<source> Capture Switch", or just "Capture Switch".
    QList<snd_ctl_elem_id> switches;
    for (unsigned int i = 0; i < list.used; ++i) {
        const QByteArray name(reinterpret_cast<const char *>(ids[i].name));
        if (ids[i].iface == SNDRV_CTL_ELEM_IFACE_MIXER && name.endsWith("Capture Switch"))
            switches.append(ids[i]);
    }

    return switches;
}

static bool writeSwitch(int fd, const snd_ctl_elem_id &id, bool on)
{
    snd_ctl_elem_info info = {};
    info.id = id;
    if (::ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) < 0 || info.type != SNDRV_CTL_ELEM_TYPE_BOOLEAN)
        return false;

    snd_ctl_elem_value value = {};
    value.id = info.id;
    for (unsigned int i = 0; i < info.count && i < std::size(value.value.integer.value); ++i)
        value.value.integer.value[i] = on ? 1 : 0;

    return ::ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &value) >= 0;
}

CaptureSwitches::CaptureSwitches(const QString &deviceDirectory, QObject *parent)
    : QObject(parent)
    , m_deviceDirectory(deviceDirectory)
{
}

CaptureSwitches::~CaptureSwitches()
{
    for (int fd : std::as_const(m_cards))
        ::close(fd);

    if (m_inotify >= 0)
        ::close(m_inotify);
}

void CaptureSwitches::setLocked(bool locked)
{
    if (m_locked == locked)
        return;

    m_locked = locked;

    if (!locked) {
        const QStringList names = m_cards.keys();
        for (const QString &name : names)
            unlockCard(name);

        return;
    }

    // Sound cards plugged in while locked are locked as their control node appears.
    if (m_inotify < 0) {
        m_inotify = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (m_inotify >= 0 && ::inotify_add_watch(m_inotify, QFile::encodeName(m_deviceDirectory).constData(),
                                                  IN_CREATE | IN_DELETE) >= 0) {
            m_notifier = new QSocketNotifier(m_inotify, QSocketNotifier::Read, this);
            connect(m_notifier, &QSocketNotifier::activated, this, &CaptureSwitches::readEvents);
        } else {
            qWarning() << "cutefish-privacy: cannot watch" << m_deviceDirectory << ::strerror(errno);
        }
    }

    const QStringList names = QDir(m_deviceDirectory).entryList({QStringLiteral("controlC*")}, QDir::System);
    for (const QString &name : names)
        lockCard(name);
}

void CaptureSwitches::lockCard(const QString &name)
{
    if (m_cards.contains(name))
        return;

    const QByteArray path = QFile::encodeName(m_deviceDirectory + QLatin1Char('/') + name);
    const int fd = ::open(path.constData(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        qWarning() << "cutefish-privacy: cannot open" << path << ::strerror(errno);
        return;
    }

    const QList<snd_ctl_elem_id> switches = captureSwitches(fd);
    if (switches.isEmpty()) {
        qInfo() << "cutefish-privacy:" << name << "has no capture switch";
        ::close(fd);
        return;
    }

    for (snd_ctl_elem_id id : switches) {
        // Locked before the write, so nothing turns it back on in between.
        if (::ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LOCK, &id) < 0)
            qWarning() << "cutefish-privacy: cannot lock" << name << reinterpret_cast<const char *>(id.name)
                       << ::strerror(errno);

        if (!writeSwitch(fd, id, false))
            qWarning() << "cutefish-privacy: cannot turn off" << name << reinterpret_cast<const char *>(id.name)
                       << ::strerror(errno);
    }

    m_cards.insert(name, fd);
}

void CaptureSwitches::unlockCard(const QString &name)
{
    const int fd = m_cards.take(name);

    const QList<snd_ctl_elem_id> switches = captureSwitches(fd);
    for (snd_ctl_elem_id id : switches) {
        writeSwitch(fd, id, true);
        ::ioctl(fd, SNDRV_CTL_IOCTL_ELEM_UNLOCK, &id);
    }

    ::close(fd);
}

void CaptureSwitches::readEvents()
{
    alignas(inotify_event) char buffer[4096];

    for (;;) {
        const ssize_t length = ::read(m_inotify, buffer, sizeof(buffer));
        if (length <= 0)
            break;

        for (ssize_t offset = 0; offset < length;) {
            const auto *event = reinterpret_cast<const inotify_event *>(buffer + offset);
            offset += sizeof(inotify_event) + event->len;

            const QString name = event->len ? QFile::decodeName(event->name) : QString();
            if (!name.startsWith(QLatin1String("controlC")))
                continue;

            if (event->mask & IN_DELETE) {
                if (m_cards.contains(name))
                    ::close(m_cards.take(name));
            } else if (m_locked) {
                lockCard(name);
            }
        }
    }
}
