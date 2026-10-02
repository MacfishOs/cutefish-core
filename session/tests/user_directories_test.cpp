// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "userdirectories.h"

class UserDirectoriesTest : public QObject
{
    Q_OBJECT

private slots:

    void firstLogin()
    {
        QTemporaryDir home;
        const auto config = home.filePath(".config");
        UserDirectories::ensure(home.path(), config);

        const QStringList names {"Desktop", "Documents", "Downloads", "Music",
                                 "Pictures", "Public", "Templates", "Videos"};
        QCOMPARE(QDir(home.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot), names);

        QFile file(config + "/user-dirs.dirs");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(),
                 QByteArray("XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
                            "XDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\n"
                            "XDG_TEMPLATES_DIR=\"$HOME/Templates\"\n"
                            "XDG_PUBLICSHARE_DIR=\"$HOME/Public\"\n"
                            "XDG_DOCUMENTS_DIR=\"$HOME/Documents\"\n"
                            "XDG_MUSIC_DIR=\"$HOME/Music\"\n"
                            "XDG_PICTURES_DIR=\"$HOME/Pictures\"\n"
                            "XDG_VIDEOS_DIR=\"$HOME/Videos\"\n"));

        qputenv("HOME", home.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", config.toUtf8());
        QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                 home.filePath("Downloads"));
    }

    void existingFileIsKept()
    {
        QTemporaryDir home;
        const auto config = home.filePath(".config");
        QVERIFY(QDir().mkpath(config));
        const QByteArray contents = "XDG_DESKTOP_DIR=\"$HOME/Schreibtisch\"\n";
        QFile file(config + "/user-dirs.dirs");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
        file.close();

        UserDirectories::ensure(home.path(), config);

        QCOMPARE(QDir(home.path()).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot),
                 QStringList {".config"});
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), contents);
    }
};
QTEST_GUILESS_MAIN(UserDirectoriesTest)
#include "user_directories_test.moc"
