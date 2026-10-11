// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "usbcameras.h"

#include <QDebug>
#include <QDir>
#include <QFile>

static const QByteArray kVideoClass = QByteArrayLiteral("0e");

static QByteArray readAttribute(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();

    return file.readAll().trimmed();
}

static bool writeAttribute(const QString &path, const QByteArray &value)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(value) != value.size()) {
        qWarning() << "cutefish-privacy: cannot write" << path << file.errorString();
        return false;
    }

    return true;
}

UsbCameras::UsbCameras(const QString &sysfs)
    : m_sysfs(sysfs)
{
}

void UsbCameras::setAuthorized(bool authorized)
{
    const QDir devices(m_sysfs + QStringLiteral("/bus/usb/devices"));
    const QByteArray value = authorized ? QByteArrayLiteral("1") : QByteArrayLiteral("0");
    QStringList probe;

    // Interfaces are named bus-port:configuration.interface; sorted, the video
    // control interface comes before the streaming ones its driver claims.
    for (const QString &name : devices.entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!name.contains(QLatin1Char(':')))
            continue;

        const QString path = devices.filePath(name);
        if (readAttribute(path + QStringLiteral("/bInterfaceClass")) != kVideoClass)
            continue;

        if (readAttribute(path + QStringLiteral("/authorized")) == value)
            continue;

        if (writeAttribute(path + QStringLiteral("/authorized"), value) && authorized)
            probe.append(name);
    }

    // Authorizing an interface does not bind a driver to it again.
    for (const QString &name : std::as_const(probe))
        writeAttribute(m_sysfs + QStringLiteral("/bus/usb/drivers_probe"), name.toLatin1());
}
