#include "ddcworker.h"

#include <QDebug>
#include <QFile>
#include <QThread>

#include <cerrno>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

constexpr int DdcAddress = 0x37;
constexpr quint8 HostAddress = 0x51;
constexpr quint8 BrightnessCode = 0x10;
constexpr int Attempts = 3;

class I2cDevice
{
public:
    explicit I2cDevice(const QString &path)
        : m_fd(::open(QFile::encodeName(path).constData(), O_RDWR | O_CLOEXEC))
    {
        // EBUSY means a kernel ddcci driver owns the bus and exposes its own backlight.
        if (m_fd >= 0 && ioctl(m_fd, I2C_SLAVE, DdcAddress) < 0) {
            ::close(m_fd);
            m_fd = -1;
        }
    }

    ~I2cDevice()
    {
        if (m_fd >= 0)
            ::close(m_fd);
    }

    bool isValid() const { return m_fd >= 0; }

    bool write(const QByteArray &payload)
    {
        QByteArray message;
        message.append(char(HostAddress));
        message.append(char(0x80 | payload.size()));
        message.append(payload);
        // The checksum covers the destination address 0x6e, which the kernel sends.
        quint8 checksum = 0x6e;
        for (char byte : std::as_const(message))
            checksum ^= quint8(byte);
        message.append(char(checksum));
        return ::write(m_fd, message.constData(), message.size()) == message.size();
    }

    bool getVcp(quint8 code, int *value, int *maximum)
    {
        if (!write(QByteArray {"\x01", 1} + char(code)))
            return false;
        QThread::msleep(40);

        quint8 reply[11];
        if (::read(m_fd, reply, sizeof(reply)) != sizeof(reply))
            return false;
        quint8 checksum = 0x50;
        for (int i = 0; i < 10; ++i)
            checksum ^= reply[i];
        if (reply[0] != 0x6e || reply[1] != 0x88 || reply[2] != 0x02 || reply[3] != 0x00
            || reply[4] != code || checksum != reply[10])
            return false;

        *maximum = (reply[6] << 8) | reply[7];
        *value = (reply[8] << 8) | reply[9];
        return *maximum > 0;
    }

    bool setVcp(quint8 code, int value)
    {
        const QByteArray payload {"\x03", 1};
        const bool ok =
                write(payload + char(code) + char((value >> 8) & 0xff) + char(value & 0xff));
        // DDC/CI requires a 50 ms gap before the next message.
        QThread::msleep(50);
        return ok;
    }

private:
    int m_fd;
};

} // namespace

DdcWorker::DdcWorker(QObject *parent)
    : QObject(parent)
{
}

void DdcWorker::requestValue(const QString &device, int value)
{
    QMutexLocker locker(&m_mutex);
    m_pending.insert(device, value);
    if (!m_flushQueued) {
        m_flushQueued = true;
        QMetaObject::invokeMethod(this, &DdcWorker::flush, Qt::QueuedConnection);
    }
}

void DdcWorker::flush()
{
    for (;;) {
        QString device;
        int value = 0;
        {
            QMutexLocker locker(&m_mutex);
            if (m_pending.isEmpty()) {
                m_flushQueued = false;
                return;
            }
            auto it = m_pending.begin();
            device = it.key();
            value = it.value();
            m_pending.erase(it);
        }

        I2cDevice i2c(device);
        if (!i2c.isValid() || !i2c.setVcp(BrightnessCode, value))
            qWarning() << "DDC/CI brightness write failed on" << device;
    }
}

void DdcWorker::probe(quint64 generation, const QList<DdcDisplay> &candidates)
{
    QList<DdcDisplay> displays;
    for (DdcDisplay display : candidates) {
        I2cDevice i2c(display.device);
        if (!i2c.isValid())
            continue;
        for (int attempt = 0; attempt < Attempts; ++attempt) {
            if (i2c.getVcp(BrightnessCode, &display.value, &display.maximum)) {
                displays.append(display);
                break;
            }
            QThread::msleep(50);
        }
    }
    emit probed(generation, displays);
}
