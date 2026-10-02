#include "shellconnection.h"

#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <QDebug>
#include <QElapsedTimer>
#include <QThread>

namespace {
// One byte of payload so the ancillary data has a datagram to ride on.
bool sendFd(int socket, int fd)
{
    char payload = 'S';
    iovec iov {};
    iov.iov_base = &payload;
    iov.iov_len = 1;

    union {
        char buffer[CMSG_SPACE(sizeof(int))];
        cmsghdr alignment;
    } control {};

    msghdr message {};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control.buffer;
    message.msg_controllen = sizeof(control.buffer);

    cmsghdr* header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(header), &fd, sizeof(fd));

    return sendmsg(socket, &message, MSG_NOSIGNAL) == 1;
}
} // namespace

ShellConnection::ShellConnection(QObject* parent)
    : QObject(parent)
{
}

ShellConnection::~ShellConnection()
{
    if (m_fd >= 0)
        close(m_fd);
}

bool ShellConnection::connectToCompositor(const QString& path, int timeoutMs)
{
    const QByteArray native = path.toUtf8();
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    if (static_cast<size_t>(native.size()) >= sizeof(address.sun_path)) {
        qWarning() << "Compositor control socket path is too long:" << path;
        return false;
    }
    std::memcpy(address.sun_path, native.constData(), native.size());

    // The compositor creates the socket while it is starting; the ready
    // notification only says the Wayland display is up.
    QElapsedTimer timer;
    timer.start();
    while (true) {
        const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0)
            return false;

        if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
            m_fd = fd;
            break;
        }
        close(fd);

        if (timer.elapsed() >= timeoutMs) {
            qWarning() << "Cannot reach the compositor control socket" << path;
            return false;
        }
        QThread::msleep(50);
    }

    return true;
}

int ShellConnection::createShellConnection()
{
    if (m_fd < 0)
        return -1;

    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0) {
        qWarning() << "Cannot create the shell Wayland socketpair";
        return -1;
    }

    const bool sent = sendFd(m_fd, pair[0]);
    close(pair[0]);
    if (!sent) {
        qWarning() << "Cannot pass the shell connection to the compositor";
        close(pair[1]);
        return -1;
    }

    return pair[1];
}
