#pragma once

#include <QObject>
#include <QString>

class QSocketNotifier;

/**
 * The session's end of the compositor control channel.
 *
 * The session, not the compositor, launches the shell. For every launch it
 * creates a Wayland socketpair, sends one end to the compositor over
 * SCM_RIGHTS - the compositor turns it into a wl_client and treats that client,
 * and only that client, as the trusted shell - and hands the other end to the
 * new cutefish-shell process as WAYLAND_SOCKET.
 *
 * The channel is held open for the life of the session so a shell restart needs
 * no compositor restart.
 */
class ShellConnection : public QObject
{
    Q_OBJECT

public:
    explicit ShellConnection(QObject* parent = nullptr);
    ~ShellConnection() override;

    //! Connects to the compositor's control socket, retrying for @p timeoutMs
    //! while the compositor is still creating it.
    bool connectToCompositor(const QString& path, int timeoutMs = 5000);

    bool isConnected() const { return m_fd >= 0; }

    /**
     * Creates a Wayland socketpair, gives one end to the compositor and returns
     * the other for the shell process. Returns -1 on failure; the caller owns
     * the returned descriptor.
     */
    int createShellConnection();

signals:
    //! The compositor closed the channel, which means it is gone.
    void disconnected();

private:
    void onReadable();

    int m_fd = -1;
    QSocketNotifier* m_notifier = nullptr;
};
