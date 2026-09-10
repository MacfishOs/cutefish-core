#include "waylandidle.h"

#include <QSignalSpy>
#include <QSocketNotifier>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <wayland-server.h>

#include "ext-idle-notify-v1-server-protocol.h"

class WaylandIdleTest : public QObject
{
    Q_OBJECT
private:
    struct Notification {
        wl_resource *resource;
        uint32_t timeout;
    };

    QList<Notification *> notifications;
    wl_display *server = nullptr;
    std::unique_ptr<QSocketNotifier> notifier;
    QTemporaryDir runtime;
    QByteArray previousRuntime;
    QByteArray previousDisplay;

    void startServer()
    {
        server = wl_display_create();
        QVERIFY(server);
        QVERIFY(wl_display_add_socket(server, "idle-test") == 0);
        wl_global_create(server, &wl_seat_interface, 1, nullptr,
                         [](wl_client *client, void *, uint32_t, uint32_t id) {
                             auto *resource = wl_resource_create(client, &wl_seat_interface, 1, id);
                             wl_resource_set_implementation(resource, nullptr, nullptr, nullptr);
                         });
        wl_global_create(
                server, &ext_idle_notifier_v1_interface, 1, this,
                [](wl_client *client, void *data, uint32_t, uint32_t id) {
                    auto *resource =
                            wl_resource_create(client, &ext_idle_notifier_v1_interface, 1, id);
                    static const struct ext_idle_notifier_v1_interface implementation = {
                            [](wl_client *, wl_resource *resource) {
                                wl_resource_destroy(resource);
                            },
                            [](wl_client *client, wl_resource *manager, uint32_t id,
                               uint32_t timeout, wl_resource *) {
                                auto *self = static_cast<WaylandIdleTest *>(
                                        wl_resource_get_user_data(manager));
                                auto *resource = wl_resource_create(
                                        client, &ext_idle_notification_v1_interface, 1, id);
                                auto *notification = new Notification {resource, timeout};
                                self->notifications.append(notification);
                                static const struct ext_idle_notification_v1_interface
                                        implementation = {[](wl_client *, wl_resource *resource) {
                                            wl_resource_destroy(resource);
                                        }};
                                wl_resource_set_implementation(
                                        resource, &implementation, self, [](wl_resource *resource) {
                                            auto *self = static_cast<WaylandIdleTest *>(
                                                    wl_resource_get_user_data(resource));
                                            for (auto *notification : self->notifications) {
                                                if (notification->resource == resource) {
                                                    self->notifications.removeOne(notification);
                                                    delete notification;
                                                    break;
                                                }
                                            }
                                        });
                            }};
                    wl_resource_set_implementation(resource, &implementation, data, nullptr);
                });
        auto *loop = wl_display_get_event_loop(server);
        notifier = std::make_unique<QSocketNotifier>(wl_event_loop_get_fd(loop),
                                                     QSocketNotifier::Read);
        connect(notifier.get(), &QSocketNotifier::activated, this, [this, loop] {
            wl_event_loop_dispatch(loop, 0);
            wl_display_flush_clients(server);
        });
    }

    void stopServer()
    {
        notifier.reset();
        wl_display_destroy_clients(server);
        wl_display_destroy(server);
        server = nullptr;
    }

private Q_SLOTS:

    void init()
    {
        QVERIFY(runtime.isValid());
        previousRuntime = qgetenv("XDG_RUNTIME_DIR");
        previousDisplay = qgetenv("WAYLAND_DISPLAY");
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
        qputenv("WAYLAND_DISPLAY", "idle-test");
        startServer();
    }

    void cleanup()
    {
        if (server)
            stopServer();
        qputenv("XDG_RUNTIME_DIR", previousRuntime);
        qputenv("WAYLAND_DISPLAY", previousDisplay);
    }

    void dispatchRemoveAndReconnect()
    {
        WaylandIdle idle;
        QSignalSpy timeouts(&idle, &WaylandIdle::timeoutReached);
        QSignalSpy resumes(&idle, &WaylandIdle::resumed);
        const int id = idle.addTimeout(250);
        QTRY_COMPARE(notifications.size(), 1);
        QCOMPARE(notifications.front()->timeout, 250u);
        ext_idle_notification_v1_send_idled(notifications.front()->resource);
        wl_display_flush_clients(server);
        QTRY_COMPARE(timeouts.size(), 1);
        QCOMPARE(timeouts.front(), QVariantList({id, 250}));
        ext_idle_notification_v1_send_resumed(notifications.front()->resource);
        wl_display_flush_clients(server);
        QTRY_COMPARE(resumes.size(), 1);
        idle.removeTimeout(id);
        QTRY_COMPARE(notifications.size(), 0);
        const int retained = idle.addTimeout(700);
        QTRY_COMPARE(notifications.size(), 1);
        stopServer();
        QTRY_COMPARE(resumes.size(), 2);
        startServer();
        QTRY_COMPARE_WITH_TIMEOUT(notifications.size(), 1, 3000);
        QCOMPARE(notifications.front()->timeout, 700u);
        ext_idle_notification_v1_send_idled(notifications.front()->resource);
        wl_display_flush_clients(server);
        QTRY_COMPARE(timeouts.size(), 2);
        QCOMPARE(timeouts.back(), QVariantList({retained, 700}));
    }
};

QTEST_GUILESS_MAIN(WaylandIdleTest)
#include "waylandidle_test.moc"
