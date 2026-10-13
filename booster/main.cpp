// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "booster/protocol.h"
#include "cutefishapplication.h"

#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSocketNotifier>

#include <private/qcoreapplication_p.h>

#include <dlfcn.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <vector>

// QCoreApplicationPrivate keeps a reference to this count, so it has to live
// as long as the application and is how the launch replaces it.
static int s_argc = 1;
static char s_name[] = "cutefish-booster";
static char *s_argv[] = {s_name, nullptr};

static QByteArray receive(int fd)
{
    const ssize_t size = ::recv(fd, nullptr, 0, MSG_PEEK | MSG_TRUNC);
    if (size <= 0)
        return {};

    QByteArray data(size, Qt::Uninitialized);
    if (::recv(fd, data.data(), size, 0) != size)
        return {};

    return data;
}

static bool reply(int fd, char message)
{
    return ::send(fd, &message, 1, MSG_NOSIGNAL) == 1;
}

// A package update has replaced a library this process maps: a module built
// against the new one must not meet the old copy.
static bool mapsReplacedFiles()
{
    QFile maps(QStringLiteral("/proc/self/maps"));
    if (!maps.open(QIODevice::ReadOnly))
        return true;

    // A /proc file has no size, so atEnd() is true from the start.
    const QList<QByteArray> lines = maps.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (line.endsWith("(deleted)") && line.contains(" /usr/"))
            return true;
    }

    return false;
}

static void preload(QQmlEngine *engine)
{
    QQmlComponent component(engine, QUrl(QStringLiteral("qrc:/booster/preload.qml")));
    if (component.isError()) {
        qWarning() << "cutefish-booster:" << component.errors();
        return;
    }

    delete component.create();
}

// Overwrites the command line the booster was started with, so that
// /proc/<pid>/cmdline and comm name the application.
static void rename(int argc, char **argv, const QStringList &arguments)
{
    char *area = argv[0];
    const size_t size = argv[argc - 1] + std::strlen(argv[argc - 1]) + 1 - area;

    QByteArray cmdline;
    for (const QString &argument : arguments)
        cmdline += argument.toLocal8Bit() + '\0';

    std::memset(area, 0, size);
    std::memcpy(area, cmdline.constData(), qMin<size_t>(cmdline.size(), size - 1));

    ::prctl(PR_SET_NAME, QFileInfo(arguments.first()).fileName().toLocal8Bit().constData());
}

// Gives the application object the identity it would have had if the
// executable had been started directly.
static void adopt(const BoosterProtocol::LaunchRequest &request, std::vector<char *> &argv)
{
    auto *d = static_cast<QCoreApplicationPrivate *>(QObjectPrivate::get(qApp));
    d->argv = argv.data();
    s_argc = static_cast<int>(argv.size()) - 1;

    // applicationFilePath() drops its cache when argv[0] changes, reading
    // /proc/self/exe, which is the booster; prime it, then overwrite it.
    QCoreApplication::applicationFilePath();
    QCoreApplicationPrivate::setApplicationFilePath(request.executable);
    d->cachedApplicationDirPath = QFileInfo(request.executable).path();

    QCoreApplication::setApplicationName(QFileInfo(request.executable).fileName());
}

int main(int argc, char *argv[])
{
    // cutefish-booster --socket <fd> <padding that the application's command line overwrites>
    if (argc < 3 || std::strcmp(argv[1], "--socket") != 0) {
        qWarning("cutefish-booster is started by cutefish-appruntime");
        return 1;
    }
    const int socket = std::atoi(argv[2]);

    Cutefish::Application::prepare();
    auto *app = new QGuiApplication(s_argc, s_argv);
    auto *engine = new QQmlEngine;
    preload(engine);

    if (!reply(socket, BoosterProtocol::Ready))
        return 1;

    // Keep serving the display and the bus until the runtime hands over an
    // application; a plain loop, so that nothing sees the application quit.
    BoosterProtocol::LaunchRequest request;
    QFunctionPointer entry = nullptr;
    QEventLoop loop;
    QSocketNotifier notifier(socket, QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &loop, [&] {
        notifier.setEnabled(false);

        if (!BoosterProtocol::decode(receive(socket), &request)) {
            loop.exit(1);
            return;
        }

        void *module = nullptr;
        if (!mapsReplacedFiles())
            module = ::dlopen(QFile::encodeName(request.module).constData(), RTLD_NOW | RTLD_LOCAL);
        if (module)
            entry = reinterpret_cast<QFunctionPointer>(::dlsym(module, "cutefish_application_main"));
        if (!entry) {
            if (module)
                qWarning() << "cutefish-booster:" << ::dlerror();
            reply(socket, BoosterProtocol::Refused);
            loop.exit(1);
            return;
        }

        // The runtime that stopped waiting has started the application itself.
        loop.exit(reply(socket, BoosterProtocol::Accepted) ? 0 : 1);
    });

    if (loop.exec() != 0)
        return 0;

    ::close(socket);

    // Qt's Wayland plugin reads the token when the first window is shown.
    if (!request.activationToken.isEmpty())
        qputenv("XDG_ACTIVATION_TOKEN", request.activationToken.toUtf8());

    if (!request.workingDirectory.isEmpty()
        && ::chdir(QFile::encodeName(request.workingDirectory).constData()) != 0)
        qWarning() << "cutefish-booster: cannot enter" << request.workingDirectory;

    QList<QByteArray> arguments;
    std::vector<char *> applicationArgv;
    for (const QString &argument : request.arguments)
        arguments.append(argument.toLocal8Bit());
    for (QByteArray &argument : arguments)
        applicationArgv.push_back(argument.data());
    applicationArgv.push_back(nullptr);

    rename(argc, argv, request.arguments);
    adopt(request, applicationArgv);

    // The preload engine stays: with it gone, the application's engine loads
    // part of what it had prepared again.
    const auto applicationMain = reinterpret_cast<Cutefish::Application::Entry>(entry);
    const int result = applicationMain(s_argc, applicationArgv.data());
    delete engine;
    delete app;
    return result;
}
