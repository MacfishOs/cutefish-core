#include "waylandidle.h"

#include <QHash>
#include <QSocketNotifier>
#include <QTimer>
#include <cerrno>
#include <wayland-client.h>

#include "ext-idle-notify-v1-client-protocol.h"

class WaylandIdle::Private : public QObject
{
public:
    struct Timeout {
        Private *owner;
        int id;
        int milliseconds;
        ext_idle_notification_v1 *proxy = nullptr;
    };

    explicit Private(WaylandIdle *owner)
        : q(owner)
    {
        retry.setSingleShot(true);
        retry.setInterval(1000);
        connect(&retry, &QTimer::timeout, this, [this] {
            start();
        });
        QTimer::singleShot(0, this, [this] {
            start();
        });
    }

    ~Private() override
    {
        reset();
        qDeleteAll(timeouts);
    }

    void reset()
    {
        delete read;
        delete write;
        read = write = nullptr;
        for (auto *timeout : timeouts) {
            if (timeout->proxy)
                ext_idle_notification_v1_destroy(timeout->proxy);
            timeout->proxy = nullptr;
        }
        if (manager)
            ext_idle_notifier_v1_destroy(manager);
        if (seat)
            wl_seat_destroy(seat);
        if (registry)
            wl_registry_destroy(registry);
        if (display)
            wl_display_disconnect(display);
        manager = nullptr;
        seat = nullptr;
        registry = nullptr;
        display = nullptr;
        seatName = managerName = 0;
    }

    void failed()
    {
        if (failing)
            return;
        failing = true;
        if (read)
            read->setEnabled(false);
        if (write)
            write->setEnabled(false);
        // Protocol callbacks must finish before their listener data is freed.
        QTimer::singleShot(0, this, [this] {
            reset();
            failing = false;
            emit q->resumed();
            retry.start();
        });
    }

    void flush()
    {
        if (!display || failing)
            return;
        if (wl_display_flush(display) < 0) {
            if (errno == EAGAIN)
                write->setEnabled(true);
            else
                failed();
        } else {
            write->setEnabled(false);
        }
    }

    void bindTimeout(Timeout *timeout)
    {
        if (!manager || !seat || timeout->proxy || failing)
            return;
        timeout->proxy = ext_idle_notifier_v1_get_idle_notification(
                manager, uint32_t(timeout->milliseconds), seat);
        static const ext_idle_notification_v1_listener listener = {
                [](void *data, ext_idle_notification_v1 *) {
                    auto *timeout = static_cast<Timeout *>(data);
                    emit timeout->owner->q->timeoutReached(timeout->id, timeout->milliseconds);
                },
                [](void *data, ext_idle_notification_v1 *) {
                    auto *timeout = static_cast<Timeout *>(data);
                    emit timeout->owner->q->resumed();
                }};
        ext_idle_notification_v1_add_listener(timeout->proxy, &listener, timeout);
    }

    void start()
    {
        const auto name = qgetenv("WAYLAND_DISPLAY");
        display = wl_display_connect(name.isEmpty() ? "wayland-0" : name.constData());
        if (!display) {
            retry.start();
            return;
        }
        read = new QSocketNotifier(wl_display_get_fd(display), QSocketNotifier::Read, this);
        write = new QSocketNotifier(wl_display_get_fd(display), QSocketNotifier::Write, this);
        write->setEnabled(false);
        connect(read, &QSocketNotifier::activated, this, [this] {
            if (wl_display_dispatch(display) < 0)
                failed();
            else
                flush();
        });
        connect(write, &QSocketNotifier::activated, this, [this] {
            flush();
        });
        registry = wl_display_get_registry(display);
        static const wl_registry_listener listener = {
                [](void *data, wl_registry *registry, uint32_t name, const char *interface,
                   uint32_t) {
                    auto *self = static_cast<Private *>(data);
                    if (QByteArray(interface) == "wl_seat" && !self->seat) {
                        self->seatName = name;
                        self->seat = static_cast<wl_seat *>(
                                wl_registry_bind(registry, name, &wl_seat_interface, 1));
                    } else if (QByteArray(interface) == "ext_idle_notifier_v1" && !self->manager) {
                        self->managerName = name;
                        self->manager = static_cast<ext_idle_notifier_v1 *>(wl_registry_bind(
                                registry, name, &ext_idle_notifier_v1_interface, 1));
                    }
                    for (auto *timeout : self->timeouts)
                        self->bindTimeout(timeout);
                },
                [](void *data, wl_registry *, uint32_t name) {
                    auto *self = static_cast<Private *>(data);
                    if (name == self->seatName || name == self->managerName)
                        self->failed();
                }};
        wl_registry_add_listener(registry, &listener, this);
        flush();
    }

    WaylandIdle *q;
    QTimer retry;
    QHash<int, Timeout *> timeouts;
    int nextId = 0;
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_seat *seat = nullptr;
    ext_idle_notifier_v1 *manager = nullptr;
    uint32_t seatName = 0;
    uint32_t managerName = 0;
    QSocketNotifier *read = nullptr;
    QSocketNotifier *write = nullptr;
    bool failing = false;
};

WaylandIdle::WaylandIdle(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this))
{
}

WaylandIdle::~WaylandIdle() = default;

int WaylandIdle::addTimeout(int milliseconds)
{
    const int id = ++d->nextId;
    auto *timeout = new Private::Timeout {d.get(), id, qMax(0, milliseconds)};
    d->timeouts.insert(id, timeout);
    d->bindTimeout(timeout);
    d->flush();
    return id;
}

void WaylandIdle::removeTimeout(int id)
{
    auto *timeout = d->timeouts.take(id);
    if (!timeout)
        return;
    if (timeout->proxy)
        ext_idle_notification_v1_destroy(timeout->proxy);
    delete timeout;
    d->flush();
}
