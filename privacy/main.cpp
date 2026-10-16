// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "privacyservice.h"

#include "base/dbus.h"
#include "base/event_loop.h"
#include "base/logging.h"

// The udev rules test the files in here; systemd's StateDirectory creates it.
static constexpr char kStateDirectory[] = "/var/lib/cutefish-privacy";

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    cutefish::base::EventLoop loop;
    static const cutefish::base::LogCategory log("cutefish.privacy");

    PrivacyService service(kStateDirectory);

    cutefish::base::dbus::Connection *bus = cutefish::base::dbus::Connection::system(&loop);
    if (!bus || !service.exportTo(bus) || !bus->requestName("com.cutefish.Privacy")) {
        CF_WARNING(log) << "cutefish-privacy: cannot register on the system bus";
        return 1;
    }

    service.onIdle = [&loop] {
        loop.quit();
    };

    return loop.run();
}
