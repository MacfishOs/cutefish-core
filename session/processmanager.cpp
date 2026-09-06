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

#include "processmanager.h"
#include "compositorprocess.h"
#include "application.h"
#include "applicationlauncher.h"
#include "applicationruntime.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <QSettings>
#include <QDebug>
#include <QProcessEnvironment>
#include <QTimer>
#include <QDir>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>

ProcessManager::ProcessManager(Application *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_compositorProcess(new CompositorProcess(this))
{
}

ProcessManager::~ProcessManager()
{
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
    if (m_started) return;
    m_started = true;
    connect(m_compositorProcess, &CompositorProcess::ready,
            this, &ProcessManager::startAfterCompositorReady);
    connect(m_compositorProcess, &CompositorProcess::failed, this, [this](const QString &reason) {
        qCritical() << reason;
        stopProcesses(m_coreProcesses);
        QCoreApplication::exit(1);
    });
    connect(m_compositorProcess, &CompositorProcess::exited, this, [this](int code) {
        stopProcesses(m_coreProcesses);
        QCoreApplication::exit(code);
    });
    m_compositorProcess->start(
        qEnvironmentVariable("CUTEFISH_DESKTOP_BINARY", QStringLiteral("/usr/bin/cutefish-desktop")),
        qEnvironmentVariable("WAYLAND_DISPLAY", QStringLiteral("cutefish-0")));
}

void ProcessManager::startAfterCompositorReady()
{
    if (m_compositorReady) return;
    m_compositorReady = true;

    // Application autostart waits for the existing external services daemon.
    const QString serviceName = QStringLiteral("com.cutefish.Services");
    const auto servicesReady = [this]() {
        if (m_servicesWatcher) {
            m_servicesWatcher->deleteLater();
            m_servicesWatcher = nullptr;
        }
        startDesktopProcess();
    };

    m_servicesWatcher = new QDBusServiceWatcher(serviceName,
                                                QDBusConnection::sessionBus(),
                                                QDBusServiceWatcher::WatchForRegistration,
                                                this);
    connect(m_servicesWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, [servicesReady](const QString &) {
        servicesReady();
    });

    startDaemonProcess();

    if (QDBusConnectionInterface *interface = QDBusConnection::sessionBus().interface()) {
        const QDBusReply<bool> reply = interface->isServiceRegistered(serviceName);
        if (reply.isValid() && reply.value())
            servicesReady();
    }
}

void ProcessManager::logout()
{
    // Applications belong to the runtime, so ask it to quit them, then close
    // what we started ourselves.
    ApplicationRuntime *runtime = ApplicationRuntime::instance();
    if (!runtime->quitAll()) {
        for (const QString &appId : runtime->runningApplications())
            runtime->quitApplication(appId);
    }

    stopProcesses(m_coreProcesses);

    m_compositorProcess->stop();

    QCoreApplication::exit(0);
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
    if (!m_compositorReady || m_desktopStarted) return;
    m_desktopStarted = true;

    // For CutefishOS. An ordinary application, so the runtime starts it.
    if (QFile("/usr/bin/cutefish-welcome").exists() &&
            !QFile("/run/live/medium/live/filesystem.squashfs").exists()) {
        QSettings settings("cutefishos", "login");
        QStringList command{QStringLiteral("/usr/bin/cutefish-welcome")};

        if (settings.value("Finished", false).toBool())
            command << QStringLiteral("-d");

        ApplicationLauncher::startDetached(command, QString(),
                                           QStringLiteral("cutefish-welcome"));
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
            qWarning() << "Failed to start daemon:" << pair.first
                       << process->errorString();
            process->deleteLater();
        }
    }
}

void ProcessManager::loadAutoStartProcess()
{
    QStringList execList;
    const QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericConfigLocation,
                                                       QStringLiteral("autostart"),
                                                       QStandardPaths::LocateDirectory);
    for (const QString &dir : dirs) {
        const QDir d(dir);
        const QStringList fileNames = d.entryList(QStringList() << QStringLiteral("*.desktop"));
        for (const QString &file : fileNames) {
            QSettings desktop(d.absoluteFilePath(file), QSettings::IniFormat);
            desktop.beginGroup("Desktop Entry");

            if (desktop.contains("OnlyShowIn"))
                continue;

            const QString execValue = desktop.value("Exec").toString();

            if (execValue.contains("cutefish-services") ||
                    execValue.contains("cutefish-appruntime") ||
                    execValue.contains("cutefish-shell") ||
                    execValue.contains("cutefish-desktop") ||
                    execValue.contains("kwin_wayland"))
                continue;

            if (!execValue.isEmpty()) {
                execList << execValue;
            }
        }
    }

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

        ApplicationLauncher::startDetached(command);
    }
}
