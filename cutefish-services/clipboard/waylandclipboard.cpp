#include "waylandclipboard.h"

#include <cerrno>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-client.h>

#include <QByteArray>
#include <QHash>
#include <QMimeData>
#include <QSocketNotifier>
#include <QTimer>

#include "ext-data-control-v1-client-protocol.h"
#include "wlr-data-control-unstable-v1-client-protocol.h"

namespace {

constexpr qsizetype kMaxClipboardBytes = 64 * 1024 * 1024;
constexpr int kMaxFormats = 128;
constexpr int kTransferTimeoutMs = 5000;

class PipeTransfer : public QObject
{
public:
    using Completion = std::function<void(bool, QByteArray)>;

    PipeTransfer(int fd, bool writing, QByteArray bytes, qsizetype limit, Completion completion,
                 QObject* parent)
        : QObject(parent)
        , m_fd(fd)
        , m_writing(writing)
        , m_bytes(std::move(bytes))
        , m_limit(limit)
        , m_completion(std::move(completion))
        , m_notifier(fd, writing ? QSocketNotifier::Write : QSocketNotifier::Read, this)
    {
        const int flags = fcntl(fd, F_GETFL);
        struct stat status {};
        m_valid = flags >= 0 && fstat(fd, &status) == 0 &&
            (S_ISFIFO(status.st_mode) || S_ISSOCK(status.st_mode)) &&
            fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
        connect(&m_notifier, &QSocketNotifier::activated, this, [this] {
            pump();
        });
        QTimer::singleShot(kTransferTimeoutMs, this, [this] {
            finish(false);
        });
        QTimer::singleShot(0, this, [this] {
            pump();
        });
    }

    ~PipeTransfer() override
    {
        if (m_fd >= 0)
            close(m_fd);
    }

private:
    ssize_t writeBytes(const char* data, size_t size)
    {
        sigset_t mask, previous, pending;
        sigemptyset(&mask);
        sigaddset(&mask, SIGPIPE);
        if (pthread_sigmask(SIG_BLOCK, &mask, &previous) != 0) {
            errno = EIO;
            return -1;
        }
        sigpending(&pending);
        const bool alreadyPending = sigismember(&pending, SIGPIPE);
        const auto result = write(m_fd, data, size);
        const int error = errno;
        if (result < 0 && error == EPIPE && !alreadyPending) {
            timespec timeout {};
            sigtimedwait(&mask, nullptr, &timeout);
        }
        pthread_sigmask(SIG_SETMASK, &previous, nullptr);
        errno = error;
        return result;
    }

    void pump()
    {
        if (m_fd < 0)
            return;
        if (!m_valid) {
            finish(false);
            return;
        }
        for (int i = 0; i < 16; ++i) {
            char buffer[4096];
            if (m_writing && m_offset == m_bytes.size()) {
                finish(true);
                return;
            }
            const auto n = m_writing
                ? writeBytes(m_bytes.constData() + m_offset,
                             size_t(qMin<qsizetype>(4096, m_bytes.size() - m_offset)))
                : read(m_fd, buffer, sizeof(buffer));
            if (n > 0) {
                if (m_writing) {
                    m_offset += n;
                } else {
                    if (n > m_limit - m_bytes.size()) {
                        finish(false);
                        return;
                    }
                    m_bytes.append(buffer, n);
                }
            } else if (!m_writing && n == 0) {
                finish(true);
                return;
            } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return;
            } else if (n < 0 && errno == EINTR) {
                continue;
            } else {
                finish(false);
                return;
            }
        }
    }

    void finish(bool success)
    {
        if (m_fd < 0)
            return;
        m_notifier.setEnabled(false);
        close(m_fd);
        m_fd = -1;
        auto completion = std::move(m_completion);
        auto bytes = std::move(m_bytes);
        deleteLater();
        if (completion)
            completion(success, std::move(bytes));
    }

    int m_fd;
    bool m_writing;
    bool m_valid = false;
    QByteArray m_bytes;
    qsizetype m_limit;
    qsizetype m_offset = 0;
    Completion m_completion;
    QSocketNotifier m_notifier;
};

// ext and wlr data-control have identical request/event layouts.
struct Protocol {
    const wl_interface* manager;
    const wl_interface* device;
    const wl_interface* source;
    const wl_interface* offer;
    uint32_t version;
};

const Protocol ext {&ext_data_control_manager_v1_interface, &ext_data_control_device_v1_interface,
                    &ext_data_control_source_v1_interface, &ext_data_control_offer_v1_interface, 1};
const Protocol wlr {&zwlr_data_control_manager_v1_interface, &zwlr_data_control_device_v1_interface,
                    &zwlr_data_control_source_v1_interface, &zwlr_data_control_offer_v1_interface,
                    2};

void destroyProxy(wl_proxy*& proxy, uint32_t opcode)
{
    if (!proxy)
        return;
    wl_proxy_marshal_flags(proxy, opcode, nullptr, wl_proxy_get_version(proxy),
                           WL_MARSHAL_FLAG_DESTROY);
    proxy = nullptr;
}

} // namespace

class WaylandClipboard::Private : public QObject
{
public:
    explicit Private(WaylandClipboard* owner)
        : QObject(owner)
        , q(owner)
    {
        QTimer::singleShot(0, this, [this] {
            start();
        });
    }

    ~Private() override { reset(); }

    struct Offer {
        wl_proxy* proxy = nullptr;
        QStringList formats;
        bool tooManyFormats = false;

        ~Offer() { destroyProxy(proxy, 1); }
    };

    struct Source : QObject {
        wl_proxy* proxy = nullptr;
        QHash<QString, QByteArray> bytes;
        int writes = 0;

        ~Source() override { destroyProxy(proxy, 1); }
    };

    void reset()
    {
        ++generation;
        delete reader;
        reader = nullptr;
        selection.reset();
        pending.clear();
        source.reset();
        snapshot.reset();
        destroyProxy(device, 1);
        destroyProxy(manager, 2);
        if (seat) {
            wl_seat_destroy(seat);
            seat = nullptr;
        }
        if (sync) {
            wl_callback_destroy(sync);
            sync = nullptr;
        }
        if (registry) {
            wl_registry_destroy(registry);
            registry = nullptr;
        }
        delete readNotifier;
        readNotifier = nullptr;
        delete writeNotifier;
        writeNotifier = nullptr;
        if (display) {
            wl_display_flush(display);
            wl_display_disconnect(display);
            display = nullptr;
        }
    }

    void failed()
    {
        if (!display)
            return;
        reset();
        emit q->availabilityChanged();
    }

    void flush()
    {
        if (!display)
            return;
        if (wl_display_flush(display) < 0) {
            if (errno == EAGAIN)
                writeNotifier->setEnabled(true);
            else
                QTimer::singleShot(0, this, [this] {
                    failed();
                });
        } else {
            writeNotifier->setEnabled(false);
        }
    }

    void start()
    {
        const auto name = qgetenv("WAYLAND_DISPLAY");
        display = wl_display_connect(name.isEmpty() ? "wayland-0" : name.constData());
        if (!display)
            return;
        readNotifier = new QSocketNotifier(wl_display_get_fd(display), QSocketNotifier::Read, this);
        writeNotifier =
            new QSocketNotifier(wl_display_get_fd(display), QSocketNotifier::Write, this);
        writeNotifier->setEnabled(false);
        connect(writeNotifier, &QSocketNotifier::activated, this, [this] {
            flush();
        });
        connect(readNotifier, &QSocketNotifier::activated, this, [this] {
            if (wl_display_dispatch(display) < 0)
                failed();
            else
                flush();
        });
        registry = wl_display_get_registry(display);
        static const wl_registry_listener registryListener = {
            [](void* data, wl_registry* registry, uint32_t name, const char* interface,
               uint32_t version) {
                auto* self = static_cast<Private*>(data);
                if (QByteArray(interface) == "wl_seat" && !self->seat)
                    self->seat = static_cast<wl_seat*>(
                        wl_registry_bind(registry, name, &wl_seat_interface, 1));
                if (QByteArray(interface) == ext.manager->name) {
                    self->extName = name;
                    self->extVersion = qMin(version, ext.version);
                } else if (QByteArray(interface) == wlr.manager->name) {
                    self->wlrName = name;
                    self->wlrVersion = qMin(version, wlr.version);
                }
            },
            [](void* data, wl_registry*, uint32_t name) {
                auto* self = static_cast<Private*>(data);
                if (name == self->boundName)
                    QTimer::singleShot(0, self, [self] {
                        self->failed();
                    });
            }};
        wl_registry_add_listener(registry, &registryListener, this);
        sync = wl_display_sync(display);
        static const wl_callback_listener ready = {[](void* data, wl_callback* callback, uint32_t) {
            auto* self = static_cast<Private*>(data);
            wl_callback_destroy(callback);
            self->sync = nullptr;
            self->bind();
        }};
        wl_callback_add_listener(sync, &ready, this);
        flush();
    }

    void bind()
    {
        if (!seat || (!extName && !wlrName))
            return;
        protocol = extName ? &ext : &wlr;
        boundName = extName ? extName : wlrName;
        const auto version = extName ? extVersion : wlrVersion;
        manager = static_cast<wl_proxy*>(
            wl_registry_bind(registry, boundName, protocol->manager, version));
        device = wl_proxy_marshal_flags(manager, 1, protocol->device, version, 0, nullptr, seat);
        static const ext_data_control_device_v1_listener listener = {
            [](void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* proxy) {
                auto* self = static_cast<Private*>(data);
                auto offer = std::make_shared<Offer>();
                offer->proxy = reinterpret_cast<wl_proxy*>(proxy);
                static const ext_data_control_offer_v1_listener offerListener = {
                    [](void* data, ext_data_control_offer_v1*, const char* mime) {
                        auto* offer = static_cast<Offer*>(data);
                        const auto format = QString::fromUtf8(mime);
                        // SAVE_TARGETS is a clipboard-manager handshake, not a payload.
                        if (format == QStringLiteral("SAVE_TARGETS"))
                            return;
                        if (offer->formats.contains(format))
                            return;
                        if (offer->formats.size() >= kMaxFormats || format.size() > 1024) {
                            offer->tooManyFormats = true;
                            return;
                        }
                        offer->formats.append(format);
                    }};
                wl_proxy_add_listener(
                    offer->proxy,
                    reinterpret_cast<void (**)(void)>(
                        const_cast<ext_data_control_offer_v1_listener*>(&offerListener)),
                    offer.get());
                self->pending[offer->proxy] = std::move(offer);
            },
            [](void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* proxy) {
                static_cast<Private*>(data)->select(reinterpret_cast<wl_proxy*>(proxy));
            },
            [](void* data, ext_data_control_device_v1*) {
                auto* self = static_cast<Private*>(data);
                QTimer::singleShot(0, self, [self] {
                    self->failed();
                });
            },
            [](void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* proxy) {
                static_cast<Private*>(data)->pending.remove(reinterpret_cast<wl_proxy*>(proxy));
            }};
        wl_proxy_add_listener(device,
                              reinterpret_cast<void (**)(void)>(
                                  const_cast<ext_data_control_device_v1_listener*>(&listener)),
                              this);
        flush();
        emit q->availabilityChanged();
    }

    void select(wl_proxy* proxy)
    {
        ++generation;
        delete reader;
        reader = nullptr;
        snapshot.reset();
        selection = pending.take(proxy);
        if (!selection || selection->tooManyFormats) {
            emit q->dataChanged();
            return;
        }
        const auto expected = generation;
        QTimer::singleShot(kTransferTimeoutMs, this, [this, expected] {
            if (expected != generation || !collecting)
                return;
            delete reader;
            reader = nullptr;
            collecting.reset();
        });
        collecting = std::make_unique<QMimeData>();
        formatIndex = 0;
        totalBytes = 0;
        readNext();
    }

    void readNext()
    {
        if (!selection)
            return;
        if (formatIndex >= selection->formats.size()) {
            snapshot = std::move(collecting);
            emit q->dataChanged();
            return;
        }
        const QString format = selection->formats[formatIndex++];
        int fds[2];
        if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) < 0) {
            collecting.reset();
            return;
        }
        const QByteArray mime = format.toUtf8();
        wl_proxy_marshal_flags(selection->proxy, 0, nullptr, 1, 0, mime.constData(), fds[1]);
        close(fds[1]);
        const auto expected = generation;
        reader = new PipeTransfer(
            fds[0], false, {}, kMaxClipboardBytes - totalBytes,
            [this, expected, format](bool complete, QByteArray bytes) {
                reader = nullptr;
                if (expected != generation)
                    return;
                if (!complete) {
                    collecting.reset();
                    return;
                }
                totalBytes += bytes.size();
                collecting->setData(format, bytes);
                readNext();
            },
            this);
        flush();
    }

    void publish(std::unique_ptr<QMimeData> data)
    {
        if (!device || !data || data->formats().size() > kMaxFormats)
            return;
        auto next = std::make_unique<Source>();
        qsizetype size = 0;
        for (const auto& format : data->formats()) {
            auto bytes = data->data(format);
            size += bytes.size();
            if (size > kMaxClipboardBytes)
                return;
            next->bytes.insert(format, std::move(bytes));
        }
        next->proxy = wl_proxy_marshal_flags(manager, 0, protocol->source, 1, 0, nullptr);
        static const ext_data_control_source_v1_listener sourceListener = {
            [](void* data, ext_data_control_source_v1*, const char* mime, int fd) {
                auto* source = static_cast<Source*>(data);
                const auto it = source->bytes.constFind(QString::fromUtf8(mime));
                if (it == source->bytes.cend() || source->writes >= 16) {
                    close(fd);
                    return;
                }
                ++source->writes;
                new PipeTransfer(
                    fd, true, *it, kMaxClipboardBytes,
                    [source](bool, QByteArray) {
                        --source->writes;
                    },
                    source);
            },
            [](void* data, ext_data_control_source_v1*) {
                auto* source = static_cast<Source*>(data);
                source->bytes.clear();
            }};
        wl_proxy_add_listener(
            next->proxy,
            reinterpret_cast<void (**)(void)>(
                const_cast<ext_data_control_source_v1_listener*>(&sourceListener)),
            next.get());
        for (auto it = next->bytes.cbegin(); it != next->bytes.cend(); ++it) {
            const auto mime = it.key().toUtf8();
            wl_proxy_marshal_flags(next->proxy, 0, nullptr, 1, 0, mime.constData());
        }
        wl_proxy_marshal_flags(device, 0, nullptr, wl_proxy_get_version(device), 0, next->proxy);
        source = std::move(next);
        snapshot = std::move(data);
        flush();
    }

    WaylandClipboard* q;
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_callback* sync = nullptr;
    wl_seat* seat = nullptr;
    wl_proxy* manager = nullptr;
    wl_proxy* device = nullptr;
    const Protocol* protocol = nullptr;
    uint32_t extName = 0, extVersion = 0, wlrName = 0, wlrVersion = 0, boundName = 0;
    QSocketNotifier* readNotifier = nullptr;
    QSocketNotifier* writeNotifier = nullptr;
    QHash<wl_proxy*, std::shared_ptr<Offer>> pending;
    std::shared_ptr<Offer> selection;
    std::unique_ptr<Source> source;
    std::unique_ptr<QMimeData> snapshot;
    std::unique_ptr<QMimeData> collecting;
    PipeTransfer* reader = nullptr;
    uint64_t generation = 0;
    qsizetype totalBytes = 0;
    qsizetype formatIndex = 0;
};

WaylandClipboard::WaylandClipboard(QObject* parent)
    : QObject(parent)
    , d(new Private(this))
{
}

WaylandClipboard::~WaylandClipboard()
{
    delete d;
}

bool WaylandClipboard::isAvailable() const
{
    return d->device != nullptr;
}

const QMimeData* WaylandClipboard::mimeData() const
{
    return d->snapshot.get();
}

void WaylandClipboard::setMimeData(QMimeData* data)
{
    d->publish(std::unique_ptr<QMimeData>(data));
}
