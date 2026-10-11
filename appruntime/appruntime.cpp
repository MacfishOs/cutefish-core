#include "appruntime.h"
#include "applicationscope.h"
#include "languageenvironment.h"
#include "proxysettings.h"

#include "applicationregistry.h"
#include "desktopentry.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <errno.h>
#include <signal.h>
#include <unistd.h>

static const int kTerminateTimeout = 5000;
// Taken at start: once the session is gone we belong to whatever adopted us.
static const uint kSessionPid = static_cast<uint>(::getppid());

AppRuntime::AppRuntime(QObject *parent)
    : QObject(parent)
    , m_registry(ApplicationRegistry::instance())
{
    // Instances are started detached, so there is no SIGCHLD to wait for.
    m_reaper.setInterval(2000);
    connect(&m_reaper, &QTimer::timeout, this, &AppRuntime::reap);
}

QProcessEnvironment AppRuntime::applicationEnvironment()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    LanguageEnvironment::apply(environment);
    ProxyEnvironment::apply(environment);
    environment.remove(QStringLiteral("XDG_ACTIVATION_TOKEN"));
    return environment;
}

uint AppRuntime::launchApplication(const QString &appId, const QStringList &arguments)
{
    return launchApplication(appId, arguments, QString());
}

uint AppRuntime::launchApplication(const QString &appId, const QStringList &arguments,
                                   const QString &activationToken)
{
    if (appId.isEmpty())
        return 0;

    // Callers know an application either by its entry id or by the path of
    // the desktop file they read.
    DesktopEntry *entry = m_registry->byId(appId);
    if (!entry && appId.contains(QLatin1Char('/'))) {
        entry = m_registry->byPath(appId);

        if (!entry)
            entry = m_registry->byId(QFileInfo(appId).completeBaseName());
    }

    if (!entry) {
        qWarning() << "AppRuntime: no desktop entry for" << appId;
        return 0;
    }

    QStringList command = entry->commandForArguments(arguments);
    if (command.isEmpty())
        return 0;

    if (entry->terminal()) {
        QString terminal = QStandardPaths::findExecutable(QStringLiteral("x-terminal-emulator"));
        if (terminal.isEmpty())
            terminal = QStandardPaths::findExecutable(QStringLiteral("xterm"));

        if (terminal.isEmpty())
            qWarning() << "AppRuntime: no terminal emulator for" << appId;
        else
            command = QStringList{terminal, QStringLiteral("-e")} + command;
    }

    return startProcess(entry->id(), command, entry->workingDirectory(), activationToken);
}

uint AppRuntime::launchCommand(const QString &appId, const QStringList &command,
                               const QString &workingDirectory)
{
    return launchCommand(appId, command, workingDirectory, QString());
}

uint AppRuntime::launchCommand(const QString &appId, const QStringList &command,
                               const QString &workingDirectory, const QString &activationToken)
{
    if (command.isEmpty() || command.first().isEmpty())
        return 0;

    QString id = appId;
    if (id.isEmpty())
        id = QFileInfo(command.first()).fileName();

    return startProcess(id, command, workingDirectory, activationToken);
}

uint AppRuntime::startProcess(const QString &appId, const QStringList &command,
                              const QString &workingDirectory, const QString &activationToken)
{
    QProcessEnvironment environment = applicationEnvironment();
    qint64 pid = m_booster.launch(command, workingDirectory, environment, activationToken);

    if (pid == 0) {
        QProcess process;
        process.setProgram(command.first());
        process.setArguments(command.mid(1));
        if (!activationToken.isEmpty())
            environment.insert(QStringLiteral("XDG_ACTIVATION_TOKEN"), activationToken);
        process.setProcessEnvironment(environment);

        if (!workingDirectory.isEmpty())
            process.setWorkingDirectory(workingDirectory);

        if (!process.startDetached(&pid) || pid <= 0) {
            qWarning() << "AppRuntime: failed to start" << command << process.errorString();
            return 0;
        }
    }

    // Only an id the registry knows names the scope: portals read the app id
    // back from it, and a bare executable name would pass for one.
    if (DesktopEntry *entry = m_registry->byId(appId))
        ApplicationScope::adopt(entry->id(), static_cast<uint>(pid));
    m_instances.insert(static_cast<uint>(pid),
                       Instance{appId, startTime(static_cast<uint>(pid))});

    if (!m_reaper.isActive())
        m_reaper.start();

    emit applicationLaunched(appId, static_cast<uint>(pid));

    return static_cast<uint>(pid);
}

bool AppRuntime::quitApplication(const QString &appId)
{
    if (appId.isEmpty())
        return false;

    bool result = false;
    for (const uint pid : pidsForApplication(appId))
        result |= terminate(pid);

    return result;
}

// Logout path: one call instead of one per application.
bool AppRuntime::quitAll()
{
    bool result = false;
    for (const uint pid : m_instances.keys())
        result |= terminate(pid);

    return result;
}

bool AppRuntime::quitByPid(uint pid)
{
    return terminate(pid);
}

// The compositor has already ended the process itself; what is left is the
// rest of its application, such as helpers it started.
bool AppRuntime::forceQuitByPid(uint pid)
{
    const auto it = m_instances.constFind(pid);
    if (it == m_instances.constEnd())
        return false;
    // The scope exists only for an id the registry knows; see startProcess().
    DesktopEntry *entry = m_registry->byId(it.value().appId);
    if (!entry)
        return false;
    ApplicationScope::killAll(ApplicationScope::unitName(entry->id(), pid));
    return true;
}

bool AppRuntime::terminate(uint pid)
{
    if (!isSafeTarget(pid) || !isAlive(pid) || !isOwnedByUser(pid))
        return false;

    // Read before SIGTERM: an exited process reads as 0, which would let the
    // delayed SIGKILL hit whatever has recycled the pid.
    const qulonglong started = startTime(pid);

    // Never signal a process group: Qt's detached children are led by an
    // intermediate fork, so the group is not ours to interpret and a wrong
    // one takes down the whole login session.
    ::kill(static_cast<pid_t>(pid), SIGTERM);

    QTimer::singleShot(kTerminateTimeout, this, [pid, started] {
        if (isAlive(pid) && isOwnedByUser(pid) && (started == 0 || startTime(pid) == started))
            ::kill(static_cast<pid_t>(pid), SIGKILL);
    });

    if (!m_reaper.isActive())
        m_reaper.start();

    return true;
}

bool AppRuntime::isRunning(const QString &appId) const
{
    return !pidsForApplication(appId).isEmpty();
}

QStringList AppRuntime::runningApplications() const
{
    QStringList result;
    for (auto it = m_instances.constBegin(); it != m_instances.constEnd(); ++it) {
        if (!result.contains(it.value().appId))
            result.append(it.value().appId);
    }
    return result;
}

QList<uint> AppRuntime::pidsForApplication(const QString &appId) const
{
    QList<uint> result;
    for (auto it = m_instances.constBegin(); it != m_instances.constEnd(); ++it) {
        if (it.value().appId == appId)
            result.append(it.key());
    }
    return result;
}

void AppRuntime::reap()
{
    const QList<uint> pids = m_instances.keys();
    for (const uint pid : pids) {
        const Instance instance = m_instances.value(pid);

        // A live pid that no longer belongs to the process we started has
        // been recycled: the instance is gone all the same. An unknown start
        // time only leaves liveness to go by.
        if (isAlive(pid) && (instance.startTime == 0 || startTime(pid) == instance.startTime))
            continue;

        m_instances.remove(pid);
        emit applicationQuit(instance.appId, pid);
    }

    if (m_instances.isEmpty())
        m_reaper.stop();
}

bool AppRuntime::isAlive(uint pid)
{
    if (pid == 0)
        return false;

    // Detached children are not ours to wait for, so a live pid is never a
    // zombie here.
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

bool AppRuntime::isSafeTarget(uint pid)
{
    if (pid <= 1)
        return false;

    // Refuse anything that would take the session down with the application,
    // including the session's own components: they are its children, as we are.
    return pid != static_cast<uint>(::getpid())
        && pid != static_cast<uint>(::getpgrp())
        && pid != static_cast<uint>(::getsid(0))
        && parentPid(pid) != kSessionPid;
}

// The fields after comm, which is parenthesised and may itself contain spaces;
// index 0 is field 3.
static QList<QByteArray> statFields(uint pid)
{
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly))
        return {};

    const QByteArray line = stat.readLine();
    const int commEnd = line.lastIndexOf(')');
    if (commEnd < 0)
        return {};

    return line.mid(commEnd + 2).simplified().split(' ');
}

// Field 4 of /proc/pid/stat.
uint AppRuntime::parentPid(uint pid)
{
    const QList<QByteArray> fields = statFields(pid);
    return fields.size() < 2 ? 0 : fields.at(1).toUInt();
}

// Field 22 of /proc/pid/stat, in clock ticks since boot.
qulonglong AppRuntime::startTime(uint pid)
{
    const QList<QByteArray> fields = statFields(pid);
    // stat field 22 is the 20th one after comm.
    if (fields.size() < 20)
        return 0;

    return fields.at(19).toULongLong();
}

bool AppRuntime::isOwnedByUser(uint pid)
{
    const QFileInfo info(QStringLiteral("/proc/%1").arg(pid));
    return info.exists() && info.ownerId() == ::getuid();
}
