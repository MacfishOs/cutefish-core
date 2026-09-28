#include "waylandparent.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

#include <atomic>
#include <cstring>
#include <memory>
#include <thread>
#include <unistd.h>
#include <vector>

#include <wayland-server.h>
#include <xdg-dialog-v1-server-protocol.h>
#include <xdg-foreign-unstable-v2-server-protocol.h>
#include <xdg-shell-server-protocol.h>

namespace {

const char socketName[] = "wayland-parent-test";

// A compositor that accepts every request: it makes the objects a request
// creates, drops an object on its destroy request and counts the objects that
// parent a prompt. Qt's connection blocks on roundtrips, so it runs on a thread.
class Compositor
{
public:
    Compositor()
    {
        m_display = wl_display_create();
        wl_display_add_socket(m_display, socketName);
        advertise(&wl_compositor_interface, 4);
        advertise(&wl_shm_interface, 1);
        advertise(&xdg_wm_base_interface, 1);
        advertise(&zxdg_importer_v2_interface, 1);
        advertise(&xdg_wm_dialog_v1_interface, 1);
        m_thread = std::thread([this] {
            wl_event_loop *loop = wl_display_get_event_loop(m_display);
            while (m_running) {
                wl_display_flush_clients(m_display);
                wl_event_loop_dispatch(loop, 10);
            }
        });
    }

    ~Compositor()
    {
        m_running = false;
        m_thread.join();
        wl_display_destroy_clients(m_display);
        wl_display_destroy(m_display);
    }

    int imported() const { return m_imported; }
    int dialogs() const { return m_dialogs; }

private:
    struct Global {
        Compositor *compositor;
        const wl_interface *interface;
    };

    void advertise(const wl_interface *interface, int version)
    {
        m_globals.push_back(std::make_unique<Global>(Global {this, interface}));
        wl_global_create(m_display, interface, version, m_globals.back().get(),
                         [](wl_client *client, void *data, uint32_t bound, uint32_t id) {
                             auto *global = static_cast<Global *>(data);
                             wl_resource *resource = global->compositor->create(
                                     client, global->interface, bound, id);
                             if (global->interface == &wl_shm_interface) {
                                 wl_shm_send_format(resource, WL_SHM_FORMAT_ARGB8888);
                                 wl_shm_send_format(resource, WL_SHM_FORMAT_XRGB8888);
                             }
                         });
    }

    wl_resource *create(wl_client *client, const wl_interface *interface, int version, uint32_t id)
    {
        wl_resource *resource = wl_resource_create(client, interface, version, id);
        wl_resource_set_dispatcher(resource, dispatch, nullptr, this, destroyed);
        if (std::atomic<int> *count = counter(resource))
            ++*count;
        return resource;
    }

    std::atomic<int> *counter(wl_resource *resource)
    {
        const char *name = wl_resource_get_class(resource);
        if (std::strcmp(name, zxdg_imported_v2_interface.name) == 0)
            return &m_imported;
        if (std::strcmp(name, xdg_dialog_v1_interface.name) == 0)
            return &m_dialogs;
        return nullptr;
    }

    static int dispatch(const void *, void *target, uint32_t, const wl_message *message,
                        wl_argument *arguments)
    {
        auto *resource = static_cast<wl_resource *>(target);
        auto *self = static_cast<Compositor *>(wl_resource_get_user_data(resource));
        int index = 0;
        for (const char *type = message->signature; *type; ++type) {
            if (*type == '?' || (*type >= '0' && *type <= '9'))
                continue;
            if (*type == 'n') {
                self->create(wl_resource_get_client(resource), message->types[index],
                             wl_resource_get_version(resource), arguments[index].n);
            } else if (*type == 'h') {
                close(arguments[index].h);
            }
            ++index;
        }
        if (std::strcmp(message->name, "destroy") == 0)
            wl_resource_destroy(resource);
        return 0;
    }

    static void destroyed(wl_resource *resource)
    {
        auto *self = static_cast<Compositor *>(wl_resource_get_user_data(resource));
        if (std::atomic<int> *count = self->counter(resource))
            --*count;
    }

    wl_display *m_display = nullptr;
    std::vector<std::unique_ptr<Global>> m_globals;
    std::thread m_thread;
    std::atomic<bool> m_running {true};
    std::atomic<int> m_imported {0};
    std::atomic<int> m_dialogs {0};
};

} // namespace

class WaylandParentTest : public QObject
{
    Q_OBJECT

public:
    explicit WaylandParentTest(const Compositor *compositor)
        : m_compositor(compositor)
    {
    }

private Q_SLOTS:
    void hidingReleasesTheParentObjects()
    {
        QWindow window;
        window.setFlag(Qt::FramelessWindowHint);
        window.resize(320, 200);
        window.show();
        applyWaylandParent(&window, QStringLiteral("wayland:requester"), true);
        QTRY_COMPARE(m_compositor->imported(), 1);
        QTRY_COMPARE(m_compositor->dialogs(), 1);

        window.hide();
        QTRY_COMPARE(m_compositor->imported(), 0);
        QTRY_COMPARE(m_compositor->dialogs(), 0);
    }

    // The parent is applied once the surface role exists, and released when
    // the window goes away without being hidden first.
    void deletingReleasesAParentAppliedBeforeShow()
    {
        auto window = std::make_unique<QWindow>();
        window->setFlag(Qt::FramelessWindowHint);
        window->resize(320, 200);
        window->create();
        applyWaylandParent(window.get(), QStringLiteral("wayland:requester"), true);
        window->show();
        QTRY_COMPARE(m_compositor->imported(), 1);
        QTRY_COMPARE(m_compositor->dialogs(), 1);

        window.reset();
        QTRY_COMPARE(m_compositor->imported(), 0);
        QTRY_COMPARE(m_compositor->dialogs(), 0);
    }

private:
    const Compositor *m_compositor;
};

int main(int argc, char **argv)
{
    QTemporaryDir runtime;
    if (!runtime.isValid())
        return 1;
    qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
    qputenv("WAYLAND_DISPLAY", socketName);
    qputenv("QT_QPA_PLATFORM", "wayland");

    // Declared first so the client disconnects before the compositor stops.
    Compositor compositor;
    QGuiApplication application(argc, argv);
    WaylandParentTest test(&compositor);
    return QTest::qExec(&test, argc, argv);
}

#include "wayland_parent_test.moc"
