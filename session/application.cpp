/*
 * Copyright (C) 2021 CutefishOS Team.
 *
 * Author:     revenmartin <revenmartin@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "application.h"
#include "languageenvironment.h"

#include "sessionadaptor.h"

// Qt
#include <QCommandLineOption>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QSettings>
#include <QSocketNotifier>
#include <QStandardPaths>

// STL
#include <optional>

// System
#include <cerrno>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

std::optional<QStringList> getSystemdEnvironment()
{
    QStringList list;
    auto msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << QStringLiteral("org.freedesktop.systemd1.Manager") << QStringLiteral("Environment");
    auto reply = QDBusConnection::sessionBus().call(msg);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        return std::nullopt;
    }

    // Make sure the returned type is correct.
    auto arguments = reply.arguments();
    if (arguments.isEmpty() || arguments[0].userType() != qMetaTypeId<QDBusVariant>()) {
        return std::nullopt;
    }
    auto variant = qdbus_cast<QVariant>(arguments[0]);
    if (variant.typeId() != QMetaType::QStringList) {
        return std::nullopt;
    }

    return variant.toStringList();
}

bool isShellVariable(const QByteArray &name)
{
    return name == "_" || name.startsWith("SHLVL");
}

bool isSessionVariable(const QByteArray &name)
{
    // Check is variable is specific to session.
    return name == "DISPLAY" || name == "XAUTHORITY" || name == "WAYLAND_DISPLAY"
            || name == "WAYLAND_SOCKET" || //
            name.startsWith("XDG_");
}

bool isInputMethodModuleVariable(const QByteArray &name)
{
    return name == "GTK_IM_MODULE" || name == "QT_IM_MODULE";
}

void setEnvironmentVariable(const QByteArray &name, const QByteArray &value)
{
    if (qgetenv(name) != value) {
        qputenv(name, value);
    }
}

static int s_terminationSocket[2] = {-1, -1};

static void onTerminationSignal(int)
{
    const int savedErrno = errno;
    const char byte = 1;
    [[maybe_unused]] const ssize_t written = ::write(s_terminationSocket[1], &byte, 1);
    errno = savedErrno;
}

Application::Application(int &argc, char **argv)
    : QCoreApplication(argc, argv)
    , m_processManager(new ProcessManager(this))
{
    endSessionOnTerminationSignals();
    new SessionAdaptor(this);

    // connect to D-Bus and register as an object:
    QDBusConnection::sessionBus().registerService(QStringLiteral("com.cutefish.Session"));
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/Session"), this);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Cutefish Session"));
    parser.addHelpOption();

    parser.process(*this);

    createConfigDirectory();
    initLanguage();

    initEnvironments();

    if (!syncDBusEnvironment()) {
        // Startup error
        qDebug() << "Could not sync environment to dbus.";
        qApp->exit(1);
    }

    // We import systemd environment after we sync the dbus environment here.
    // Otherwise it may leads to some unwanted order of applying environment
    // variables (e.g. LANG and LC_*)
    // ref plasma
    qunsetenv("LC_ALL");
    importSystemdEnvrionment();

    QTimer::singleShot(50, this, &Application::updateUserDirs);
    QTimer::singleShot(100, m_processManager, &ProcessManager::start);
}

Application::~Application()
{
    // The process manager has no parent. Deleted here, while the application still
    // runs, it stops what it started and removes its Xwayland readiness directory.
    delete m_processManager;
}

void Application::endSessionOnTerminationSignals()
{
    // cutefish-dm and systemd end a session with SIGTERM; dying of it would leave
    // the compositor and services holding the Wayland socket against the next login.
    // A handler, unlike signalfd's blocked mask, is not inherited by child processes.
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, s_terminationSocket)
        != 0) {
        qWarning() << "Cannot handle termination signals";
        return;
    }
    auto *notifier = new QSocketNotifier(s_terminationSocket[0], QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this, notifier] {
        notifier->setEnabled(false);
        m_processManager->endSession();
    });
    struct sigaction action = {};
    action.sa_handler = onTerminationSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (const int number : {SIGTERM, SIGINT, SIGHUP})
        ::sigaction(number, &action, nullptr);
}

void Application::initEnvironments()
{
    // Set defaults
    if (qEnvironmentVariableIsEmpty("XDG_DATA_HOME"))
        qputenv("XDG_DATA_HOME",
                QDir::home().absoluteFilePath(QStringLiteral(".local/share")).toLocal8Bit());
    if (qEnvironmentVariableIsEmpty("XDG_DESKTOP_DIR"))
        qputenv("XDG_DESKTOP_DIR",
                QDir::home().absoluteFilePath(QStringLiteral("Desktop")).toLocal8Bit());
    if (qEnvironmentVariableIsEmpty("XDG_CONFIG_HOME"))
        qputenv("XDG_CONFIG_HOME",
                QDir::home().absoluteFilePath(QStringLiteral(".config")).toLocal8Bit());
    if (qEnvironmentVariableIsEmpty("XDG_CACHE_HOME"))
        qputenv("XDG_CACHE_HOME",
                QDir::home().absoluteFilePath(QStringLiteral(".cache")).toLocal8Bit());
    if (qEnvironmentVariableIsEmpty("XDG_DATA_DIRS"))
        qputenv("XDG_DATA_DIRS", "/usr/local/share/:/usr/share/");
    if (qEnvironmentVariableIsEmpty("XDG_CONFIG_DIRS"))
        qputenv("XDG_CONFIG_DIRS", "/etc/xdg");

    if (qEnvironmentVariableIsEmpty("XDG_MENU_PREFIX"))
        qputenv("XDG_MENU_PREFIX", "cutefish-");

    // Environment
    qputenv("DESKTOP_SESSION", "Cutefish");
    qputenv("XDG_CURRENT_DESKTOP", "Cutefish");
    qputenv("XDG_SESSION_DESKTOP", "Cutefish");

    // GLib's plain-text backend: cutefish-services writes the GTK appearance keys into
    // ~/.config/glib-2.0/settings/keyfile, and no dconf daemon is needed.
    qputenv("GSETTINGS_BACKEND", "keyfile");

    const QString gsettings = QStandardPaths::findExecutable(QStringLiteral("gsettings"));
    if (!gsettings.isEmpty()) {
        runSync(gsettings,
                {QStringLiteral("set"), QStringLiteral("org.gnome.desktop.wm.preferences"),
                 QStringLiteral("button-layout"), QStringLiteral("menu:minimize,maximize,close")});
    }

    // Qt
    qputenv("QT_QPA_PLATFORM", "wayland");
    qputenv("GDK_BACKEND", "wayland");
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        qputenv("WAYLAND_DISPLAY", "cutefish-0");
    qputenv("QT_QPA_PLATFORMTHEME", "cutefish");

    // ref:
    // https://stackoverflow.com/questions/34399993/qml-performance-issue-when-updating-an-item-in-presence-of-many-non-overlapping
    qputenv("QT_QPA_UPDATE_IDLE_TIME", "10");

    // im-config's /etc/profile.d script exports these for Wayland sessions; they would bypass
    // the compositor's text-input protocols. XMODIFIERS stays for Xwayland clients.
    qunsetenv("GTK_IM_MODULE");
    qunsetenv("QT_IM_MODULE");
}

void Application::initLanguage()
{
    QSettings settings(QSettings::UserScope, "cutefish", "language");
    QString value = settings.value("language", "").toString();

    // Init Language
    if (value.isEmpty()) {
        QFile file("/etc/locale.gen");
        if (file.open(QIODevice::ReadOnly)) {
            QStringList lines = QString(file.readAll()).split('\n');

            for (const QString &line : lines) {
                if (line.startsWith('#'))
                    continue;

                if (line.trimmed().isEmpty())
                    continue;

                value = line.split(' ').first().split('.').first();
            }
        }
    }

    if (value.isEmpty())
        value = "en_US";

    settings.setValue("language", value);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    LanguageEnvironment::apply(environment);
    for (const QString &name : environment.keys()) {
        if (name == "LANG" || name == "LANGUAGE" || name.startsWith("LC_"))
            qputenv(name.toUtf8().constData(), environment.value(name).toUtf8());
    }
    // Export an empty value to clear LC_ALL in an already-running D-Bus daemon.
    qputenv("LC_ALL", "");
}

bool Application::syncDBusEnvironment()
{
    int exitCode = 0;

    // At this point all environment variables are set, let's send it to the DBus session server to
    // update the activation environment
    if (!QStandardPaths::findExecutable(QStringLiteral("dbus-update-activation-environment"))
                 .isEmpty()) {
        exitCode = runSync(QStringLiteral("dbus-update-activation-environment"),
                           {QStringLiteral("--systemd"), QStringLiteral("--all")});
    }

    // --all only adds variables, so values exported by an earlier login stay in systemd.
    const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    if (!systemctl.isEmpty()) {
        runSync(systemctl,
                {QStringLiteral("--user"), QStringLiteral("unset-environment"),
                 QStringLiteral("GTK_IM_MODULE"), QStringLiteral("QT_IM_MODULE"),
                 QStringLiteral("LC_ALL")});
    }

    return exitCode == 0;
}

// Import systemd user environment.
// Systemd read ~/.config/environment.d which applies to all systemd user unit.
// But it won't work if cutefishDE is not started by systemd.
void Application::importSystemdEnvrionment()
{
    auto environment = getSystemdEnvironment();
    if (!environment) {
        return;
    }

    for (auto &envString : environment.value()) {
        const auto env = envString.toLocal8Bit();
        const int idx = env.indexOf('=');
        if (Q_UNLIKELY(idx <= 0)) {
            continue;
        }

        const auto name = env.left(idx);
        if (isShellVariable(name) || isSessionVariable(name)
            || isInputMethodModuleVariable(name) || name == "LC_ALL") {
            continue;
        }
        setEnvironmentVariable(name, env.mid(idx + 1));
    }
}

void Application::createConfigDirectory()
{
    const QString configDir =
            QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);

    if (!QDir().mkpath(configDir))
        qDebug() << "Could not create config directory XDG_CONFIG_HOME: " << configDir;
}

void Application::updateUserDirs()
{
    // bool isCutefishOS = QFile::exists("/etc/cutefishos");

    // if (!isCutefishOS)
    //     return;

    // QProcess p;
    // p.setEnvironment(QStringList() << "LC_ALL=C");
    // p.start("xdg-user-dirs-update", QStringList() << "--force");
    // p.waitForFinished(-1);
}

int Application::runSync(const QString &program, const QStringList &args, const QStringList &env)
{
    QProcess p;

    if (!env.isEmpty())
        p.setEnvironment(QProcess::systemEnvironment() << env);

    p.setProcessChannelMode(QProcess::ForwardedChannels);
    p.start(program, args);
    p.waitForFinished(-1);

    if (p.exitCode()) {
        qWarning() << program << args << "exited with code" << p.exitCode();
    }

    return p.exitCode();
}
