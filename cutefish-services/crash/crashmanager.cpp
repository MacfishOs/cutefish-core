// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "crashmanager.h"
#include "coredumpjournal.h"
#include "crashdialog.h"
#include "desktopentry.h"

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

// What cutefish-session starts and restarts; cutefish-services cannot report itself.
static const QLatin1String s_sessionPrograms[] = {
        QLatin1String("cutefish-appruntime"),
        QLatin1String("cutefish-filemanager"),
};

static bool isSessionProgram(const QString &program)
{
    for (const auto &name : s_sessionPrograms) {
        if (program == name)
            return true;
    }
    return false;
}

static bool lookupEntry(const QString &id, DesktopEntryData *data)
{
    const QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                                id + QStringLiteral(".desktop"));
    QFile file(path);
    return !path.isEmpty() && file.open(QIODevice::ReadOnly)
            && DesktopEntry::parse(id, path, file.readAll(), data);
}

// A helper process of the application may crash while its windows live on.
static bool applicationEnded(const Coredump &dump)
{
    if (dump.pid == dump.applicationPid())
        return true;
    QFile procs(QStringLiteral("/sys/fs/cgroup") + dump.cgroup + QStringLiteral("/cgroup.procs"));
    if (!procs.open(QIODevice::ReadOnly))
        return true;
    return procs.readAll().trimmed().isEmpty();
}

CrashManager::CrashManager(std::function<QQmlEngine *()> engine,
                           std::function<QString()> fixedFont, QObject *parent)
    : QObject(parent)
    , m_engine(std::move(engine))
    , m_fixedFont(std::move(fixedFont))
    , m_journal(new CoredumpJournal(this))
{
    // The QML lives in this static library's resources.
    Q_INIT_RESOURCE(crash);
    connect(m_journal, &CoredumpJournal::coredump, this, &CrashManager::onCoredump);
}

CrashManager::~CrashManager()
{
    const auto dialogs = m_dialogs;
    m_dialogs.clear();
    qDeleteAll(dialogs);
}

void CrashManager::onCoredump(const Coredump &dump)
{
    const QString appId = dump.applicationId();
    if (!appId.isEmpty()) {
        if (!applicationEnded(dump))
            return;
        if (CrashDialog *dialog = dialogFor(QStringLiteral("app:") + appId, true, appId))
            dialog->setDump(dump);
        return;
    }

    // Outside an application scope, only what the session restarts gets a dialog.
    const QString program = QFileInfo(dump.executable).fileName();
    if (!isSessionProgram(program))
        return;
    if (CrashDialog *dialog = dialogFor(QStringLiteral("process:") + program, false, program))
        dialog->setDump(dump);
}

CrashDialog *CrashManager::dialogFor(const QString &key, bool application, const QString &id)
{
    if (CrashDialog *dialog = m_dialogs.value(key))
        return dialog;

    QString name = id;
    QString icon = QStringLiteral("application-x-executable");
    DesktopEntryData data;
    if (lookupEntry(id, &data)) {
        if (!data.name.isEmpty())
            name = data.name;
        if (!data.icon.isEmpty())
            icon = data.icon;
    } else if (id == QLatin1String("cutefish-appruntime")) {
        name = tr("Application Runtime");
    }

    auto *dialog = new CrashDialog(m_engine(),
                                   application ? CrashDialog::Kind::Application
                                               : CrashDialog::Kind::SessionProcess,
                                   id, name, icon, m_fixedFont(), this);
    m_dialogs.insert(key, dialog);
    connect(dialog, &CrashDialog::finished, this, [this, key, dialog] {
        m_dialogs.remove(key);
        dialog->deleteLater();
    });
    if (!dialog->show()) {
        m_dialogs.remove(key);
        dialog->deleteLater();
        return nullptr;
    }
    return dialog;
}
