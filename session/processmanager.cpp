// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "processmanager.h"

#include <QCoreApplication>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <QDir>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <limits>

#include "application.h"
#include "applicationlauncher.h"
#include "applicationregistry.h"
#include "applicationruntime.h"
#include "autostartentries.h"
#include "compositorprocess.h"
#include "shellconnection.h"
#include "shellprocess.h"
#include "xwaylandenvironment.h"

namespace {
// Bounded restart policy for cutefish-shell. The compositor keeps running
// either way: a shell that cannot come up leaves the session with applications
// and no panels, which is recoverable, while a restart loop is not.
constexpr int kMaxShellRestarts = 5;
// A shell that stayed up this long was not crash-looping, so it gets a fresh count.
constexpr qint64 kShellStableUptimeMs = 30000;
constexpr int kShellRestartDelayMs = 1000;
constexpr int kShellRestartBackoffMs = 2000;
} // namespace

ProcessManager::ProcessManager(Application *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_compositorProcess(new CompositorProcess(this))
    , m_shellConnection(new ShellConnection(this))
    , m_shellProcess(new ShellProcess(this))
    , m_shellRestartTimer(new QTimer(this))
{
    m_shellRestartTimer->setSingleShot(true);
    connect(m_shellRestartTimer, &QTimer::timeout, this, &ProcessManager::startShell);
    connect(m_shellProcess, &QProcess::finished, this, &ProcessManager::onShellFinished);
}

ProcessManager::~ProcessManager()
{
    m_loggingOut = true;
    m_shellRestartTimer->stop();
    m_shellProcess->stop();
    stopProcesses(m_coreProcesses);
    m_compositorProcess->stop();
    QMapIterator<QString, QProcess *> i(m_coreProcesses);
    while (i.hasNext()) {
        i.next();
        delete i.value();
        m_coreProcesses[i.key()] = nullptr;
    }
}

void ProcessManager::start()
{
    if (m_started)
        return;
    m_started = true;

    m_waylandSocket = qEnvironmentVariable("WAYLAND_DISPLAY", QStringLiteral("cutefish-0"));
    const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR", QStringLiteral("/tmp"));
    m_controlSocket = QStringLiteral("%1/%2-shell.control").arg(runtimeDir, m_waylandSocket);

    qunsetenv("DISPLAY");
    qunsetenv("XAUTHORITY");
    m_xwaylandEnvironment = new XwaylandEnvironment(this);
    connect(m_xwaylandEnvironment, &XwaylandEnvironment::resolved, this, [this](bool) {
        publishXwaylandEnvironment(true);
    });
    connect(m_xwaylandEnvironment, &XwaylandEnvironment::lost, this, [this] {
        publishXwaylandEnvironment(false);
    });
    connect(m_compositorProcess, &CompositorProcess::ready, this, [this] {
        m_xwaylandEnvironment->waitForReady();
    });
    connect(m_compositorProcess, &CompositorProcess::failed, this, [this](const QString &reason) {
        qCritical() << reason;
        m_loggingOut = true;
        m_shellRestartTimer->stop();
        m_shellProcess->stop();
        stopProcesses(m_coreProcesses);
        QCoreApplication::exit(1);
    });
    // A compositor that dies takes the graphical session with it: there is no
    // display left to run on.
    connect(m_compositorProcess, &CompositorProcess::exited, this, [this](int code) {
        m_loggingOut = true;
        m_shellRestartTimer->stop();
        m_shellProcess->stop();
        stopProcesses(m_coreProcesses);
        QCoreApplication::exit(code);
    });
    m_compositorProcess->start(qEnvironmentVariable("CUTEFISH_COMPOSITOR_BINARY",
                                                    QStringLiteral("/usr/bin/cutefish-compositor")),
                               m_waylandSocket, m_controlSocket, 15000,
                               m_xwaylandEnvironment->readinessFile());
}

void ProcessManager::publishXwaylandEnvironment(bool startDesktop)
{
    if (m_loggingOut)
        return;
    auto *process = new QProcess(this);
    auto *deadline = new QTimer(process);
    deadline->setSingleShot(true);
    const auto finish = [this, process, deadline, startDesktop] {
        deadline->stop();
        process->disconnect(this);
        process->deleteLater();
        if (startDesktop && !m_loggingOut)
            startAfterCompositorReady();
    };
    connect(process, &QProcess::finished, this, [finish](int code) {
        if (code != 0)
            qWarning() << "Could not publish Xwayland environment to activation services";
        finish();
    });
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            qWarning() << "dbus-update-activation-environment is unavailable";
            finish();
        }
    });
    connect(deadline, &QTimer::timeout, process, [process] {
        process->kill();
    });
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    process->start(QStringLiteral("dbus-update-activation-environment"),
                   {QStringLiteral("--systemd"),
                    QStringLiteral("DISPLAY=") + qEnvironmentVariable("DISPLAY"),
                    QStringLiteral("XAUTHORITY=") + qEnvironmentVariable("XAUTHORITY")});
    deadline->start(3000);
}

void ProcessManager::startAfterCompositorReady()
{
    if (m_compositorReady)
        return;
    m_compositorReady = true;

    // The persistent control channel. It is what makes a shell restart possible
    // without restarting the compositor: every shell generation gets a fresh
    // Wayland socketpair over it.
    if (!m_shellConnection->connectToCompositor(m_controlSocket)) {
        qWarning() << "No compositor control channel; the shell will run without "
                      "the private Cutefish protocols";
    }

    // Application autostart waits for the existing external services daemon.
    const QString serviceName = QStringLiteral("com.cutefish.Services");
    const auto servicesReady = [this]() {
        if (m_servicesWatcher) {
            m_servicesWatcher->deleteLater();
            m_servicesWatcher = nullptr;
        }
        startDesktopProcess();
    };

    m_servicesWatcher = new QDBusServiceWatcher(serviceName, QDBusConnection::sessionBus(),
                                                QDBusServiceWatcher::WatchForRegistration, this);
    connect(m_servicesWatcher, &QDBusServiceWatcher::serviceRegistered, this,
            [servicesReady](const QString &) {
                servicesReady();
            });

    startDaemonProcess();

    if (QDBusConnectionInterface *interface = QDBusConnection::sessionBus().interface()) {
        const QDBusReply<bool> reply = interface->isServiceRegistered(serviceName);
        if (reply.isValid() && reply.value())
            servicesReady();
    }
}

void ProcessManager::startShell()
{
    if (m_loggingOut || m_shellProcess->state() != QProcess::NotRunning)
        return;

    m_shellUptime.start();
    m_shellProcess->startForSession(qEnvironmentVariable("CUTEFISH_SHELL_BINARY",
                                                         QStringLiteral("/usr/bin/cutefish-shell")),
                                    m_waylandSocket,
                                    m_shellConnection->isConnected() ? m_shellConnection : nullptr);
}

void ProcessManager::onShellFinished(int code, QProcess::ExitStatus status)
{
    if (m_loggingOut || !m_desktopStarted)
        return;

    // The compositor and every application it is showing carry on; only the
    // shell surfaces disappear, and the compositor releases the work area it
    // had reserved for them. A new connection repopulates the dock.
    if (m_shellUptime.isValid() && m_shellUptime.elapsed() >= kShellStableUptimeMs)
        m_shellRestarts = 0;
    if (m_shellRestarts >= kMaxShellRestarts) {
        qCritical() << "cutefish-shell failed" << m_shellRestarts
                    << "times; not restarting it again";
        return;
    }

    const int delay = kShellRestartDelayMs + m_shellRestarts * kShellRestartBackoffMs;
    ++m_shellRestarts;
    qWarning() << "cutefish-shell exited" << code << status << "- restarting in" << delay << "ms";
    m_shellRestartTimer->start(delay);
}

void ProcessManager::logout()
{
    endAfterApplications(Ending::Logout, [this] {
        leave();
    });
}

void ProcessManager::endAfterApplications(Ending ending, std::function<void()> then)
{
    if (m_ending)
        return;
    m_ending = true;

    const QDBusMessage quitAll = QDBusMessage::createMethodCall(
            QStringLiteral("com.cutefish.Applications1"),
            QStringLiteral("/com/cutefish/Applications1"),
            QStringLiteral("com.cutefish.Applications1"), QStringLiteral("QuitAll"));
    // No timeout: the user may take as long as they like over an unsaved document.
    auto *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(quitAll, std::numeric_limits<int>::max()),
            this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, ending, then] {
        const QDBusPendingReply<QString, QString> reply = *watcher;
        watcher->deleteLater();
        // Without the compositor's answer the session ends as it always did.
        if (reply.isError() || reply.argumentAt<0>() == QLatin1String("quit")) {
            then();
            return;
        }
        m_ending = false;
        notifyEndingStopped(ending, reply.argumentAt<0>(), reply.argumentAt<1>());
    });
}

void ProcessManager::notifyEndingStopped(Ending ending, const QString &outcome,
                                         const QString &applicationId)
{
    const DesktopEntry *entry = ApplicationRegistry::instance()->byId(applicationId);
    QString name = entry ? entry->name() : QString();
    if (name.isEmpty())
        name = applicationId.isEmpty() ? tr("An application") : applicationId;

    QString summary;
    switch (ending) {
    case Ending::Logout:
        summary = tr("Log out cancelled");
        break;
    case Ending::Restart:
        summary = tr("Restart cancelled");
        break;
    case Ending::Shutdown:
        summary = tr("Shutdown cancelled");
        break;
    }
    const QString body = outcome == QLatin1String("cancelled") ? tr("%1 cancelled it.").arg(name)
                                                               : tr("%1 did not quit.").arg(name);

    QDBusMessage notify = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.Notifications"),
            QStringLiteral("/org/freedesktop/Notifications"),
            QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    notify << QStringLiteral("cutefish-session") << uint(0)
           << (entry && !entry->icon().isEmpty() ? entry->icon()
                                                 : QStringLiteral("system-log-out"))
           << summary << body << QStringList() << QVariantMap() << -1;
    QDBusConnection::sessionBus().asyncCall(notify);
}

void ProcessManager::leave()
{
    m_loggingOut = true;
    m_shellRestartTimer->stop();

    // Under cutefish-dm the login screen comes up on its own VT first, and the
    // reply comes once it is on screen: this session then ends where nobody sees
    // it. Another display manager does not answer, and the session ends as before.
    const QDBusMessage switchToGreeter = QDBusMessage::createMethodCall(
            QStringLiteral("com.cutefish.DisplayManager"),
            QStringLiteral("/com/cutefish/DisplayManager"),
            QStringLiteral("com.cutefish.DisplayManager"), QStringLiteral("SwitchToGreeter"));
    const QDBusMessage switched = QDBusConnection::systemBus().call(switchToGreeter, QDBus::Block, 15000);
    if (switched.type() == QDBusMessage::ErrorMessage
        && switched.errorName() != QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown"))
        qWarning() << "The login screen did not come up before logout:" << switched.errorMessage();

    endSession(switched.type() == QDBusMessage::ReplyMessage);
}

void ProcessManager::endSession(bool behindLoginScreen)
{
    m_loggingOut = true;
    m_shellRestartTimer->stop();

    // Applications belong to the runtime, so ask it to quit them, then close
    // what we started ourselves.
    ApplicationRuntime *runtime = ApplicationRuntime::instance();
    if (!runtime->quitAll()) {
        for (const QString &appId : runtime->runningApplications())
            runtime->quitApplication(appId);
    }

    m_shellProcess->stop();
    stopProcesses(m_coreProcesses);

    // The kernel takes the compositor's cursor down with it; behind the login
    // screen it stays until that one's own replaces it.
    QTimer::singleShot(behindLoginScreen ? 1000 : 0, this, [this] {
        m_compositorProcess->stop();
        QCoreApplication::exit(0);
    });
}

void ProcessManager::stopProcesses(QMap<QString, QProcess *> &processes)
{
    QMapIterator<QString, QProcess *> i(processes);
    while (i.hasNext()) {
        i.next();
        QProcess *p = i.value();

        if (!p || p->state() == QProcess::NotRunning)
            continue;

        p->terminate();

        if (!p->waitForFinished(2000))
            p->kill();
    }
}

void ProcessManager::startDesktopProcess()
{
    if (!m_compositorReady || m_desktopStarted)
        return;
    m_desktopStarted = true;

    startShell();

    // Resident, so every file manager window and dialog of the session, the
    // dock's included, opens from one warm process.
    auto *fileManager = new QProcess;
    fileManager->setProcessChannelMode(QProcess::ForwardedChannels);
    fileManager->start(QStringLiteral("cutefish-filemanager"), {QStringLiteral("--daemon")});
    if (fileManager->waitForStarted()) {
        m_coreProcesses.insert(QStringLiteral("cutefish-filemanager"), fileManager);
    } else {
        qWarning() << "Failed to start the file manager daemon:" << fileManager->errorString();
        fileManager->deleteLater();
    }

    // For CutefishOS. An ordinary application, so the runtime starts it.
    if (QFile("/usr/bin/cutefish-welcome").exists()
        && !QFile("/run/live/medium/live/filesystem.squashfs").exists()) {
        QSettings settings("cutefish", "login");
        QStringList command {QStringLiteral("/usr/bin/cutefish-welcome")};

        if (settings.value("Finished", false).toBool())
            command << QStringLiteral("-d");

        ApplicationLauncher::startDetached(command, QString(), QStringLiteral("cutefish-welcome"));
    }

    // Auto start
    QTimer::singleShot(100, this, &ProcessManager::loadAutoStartProcess);
}

void ProcessManager::startDaemonProcess()
{
    QList<QPair<QString, QStringList>> list;
    // The application runtime owns every application start, so it has to be up
    // before anything that may want to launch one.
    list << qMakePair(QString("cutefish-appruntime"), QStringList());
    // This daemon provides the services used by the desktop components.
    list << qMakePair(QString("cutefish-services"), QStringList());

    for (QPair<QString, QStringList> pair : list) {
        QProcess *process = new QProcess;
        process->setProcessChannelMode(QProcess::ForwardedChannels);
        process->setProgram(pair.first);
        process->setArguments(pair.second);
        process->start();
        process->waitForStarted();

        // Add to map
        // exitCode() is not a valid way to test a process that is still
        // running. The old check discarded long-lived daemons immediately
        // after starting them.
        if (process->state() != QProcess::NotRunning) {
            m_coreProcesses.insert(pair.first, process);
        } else {
            qWarning() << "Failed to start daemon:" << pair.first << process->errorString();
            process->deleteLater();
        }
    }
}

void ProcessManager::loadAutoStartProcess()
{
    const QStringList dirs =
            QStandardPaths::locateAll(QStandardPaths::GenericConfigLocation,
                                      QStringLiteral("autostart"), QStandardPaths::LocateDirectory);
    const auto desktops =
            qEnvironmentVariable("XDG_CURRENT_DESKTOP", "Cutefish").split(':', Qt::SkipEmptyParts);
    const auto execList = autostartEntries(dirs, desktops);

    // Autostart entries are applications: the runtime starts them, and keeps
    // them quittable like any other application.
    for (const QString &exec : execList) {
        QStringList command = QProcess::splitCommand(exec);

        // Autostart entries take no files, so their field codes expand to
        // nothing rather than to a literal "%U" argument.
        command.removeIf([](const QString &argument) {
            return argument.size() == 2 && argument.startsWith(QLatin1Char('%'));
        });

        if (command.isEmpty())
            continue;

        const auto executable = QFileInfo(command.first()).fileName();
        if (QStringList {"cutefish-services", "cutefish-appruntime", "cutefish-shell",
                         "cutefish-compositor", "kwin_wayland"}
                    .contains(executable))
            continue;

        ApplicationLauncher::startDetached(command);
    }
}
