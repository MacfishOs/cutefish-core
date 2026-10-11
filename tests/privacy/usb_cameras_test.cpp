// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "usbcameras.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

static void writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).path());

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

static QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
}

class UsbCamerasTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QVERIFY(m_sysfs.isValid());

        const QString devices = m_sysfs.path() + QStringLiteral("/bus/usb/devices/");
        // A camera's control and streaming interfaces, a keyboard, and the camera's device.
        writeFile(devices + QStringLiteral("3-4:1.0/bInterfaceClass"), "0e\n");
        writeFile(devices + QStringLiteral("3-4:1.0/authorized"), "1\n");
        writeFile(devices + QStringLiteral("3-4:1.1/bInterfaceClass"), "0e\n");
        writeFile(devices + QStringLiteral("3-4:1.1/authorized"), "1\n");
        writeFile(devices + QStringLiteral("1-1:1.0/bInterfaceClass"), "03\n");
        writeFile(devices + QStringLiteral("1-1:1.0/authorized"), "1\n");
        writeFile(devices + QStringLiteral("3-4/authorized"), "1\n");
        writeFile(m_sysfs.path() + QStringLiteral("/bus/usb/drivers_probe"), "");
    }

    void offTouchesOnlyVideoInterfaces()
    {
        UsbCameras(m_sysfs.path()).setAuthorized(false);

        QCOMPARE(readFile(attribute("3-4:1.0")), QByteArray("0"));
        QCOMPARE(readFile(attribute("3-4:1.1")), QByteArray("0"));
        QCOMPARE(readFile(attribute("1-1:1.0")), QByteArray("1"));
        QCOMPARE(readFile(attribute("3-4")), QByteArray("1"));
        QCOMPARE(readFile(probe()), QByteArray());
    }

    void onAuthorizesAndProbes()
    {
        UsbCameras cameras(m_sysfs.path());
        cameras.setAuthorized(false);
        cameras.setAuthorized(true);

        QCOMPARE(readFile(attribute("3-4:1.0")), QByteArray("1"));
        QCOMPARE(readFile(attribute("3-4:1.1")), QByteArray("1"));
        // The file keeps only the last write: the streaming interface, after its control one.
        QCOMPARE(readFile(probe()), QByteArray("3-4:1.1"));
    }

    void onLeavesAuthorizedInterfacesAlone()
    {
        UsbCameras(m_sysfs.path()).setAuthorized(true);

        QCOMPARE(readFile(probe()), QByteArray());
    }

private:
    QString attribute(const char *name) const
    {
        return m_sysfs.path() + QStringLiteral("/bus/usb/devices/") + QLatin1String(name)
               + QStringLiteral("/authorized");
    }

    QString probe() const
    {
        return m_sysfs.path() + QStringLiteral("/bus/usb/drivers_probe");
    }

    QTemporaryDir m_sysfs;
};

QTEST_GUILESS_MAIN(UsbCamerasTest)

#include "usb_cameras_test.moc"
