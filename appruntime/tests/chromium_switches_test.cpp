// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "chromiumswitches.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

static void touch(const QString &path)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
}

class ChromiumSwitchesTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        QVERIFY(QDir(m_root.path()).mkpath("electron-app"));
        QVERIFY(QDir(m_root.path()).mkpath("bin"));
        QVERIFY(QDir(m_root.path()).mkpath("plain"));

        touch(m_root.filePath("electron-app/app"));
        touch(m_root.filePath("electron-app/icudtl.dat"));
        touch(m_root.filePath("electron-app/chrome_100_percent.pak"));
        QVERIFY(QFile::link(m_root.filePath("electron-app/app"), m_root.filePath("bin/app")));

        touch(m_root.filePath("plain/tool"));
        touch(m_root.filePath("plain/icudtl.dat"));
    }

    void switchFollowsTheProgram()
    {
        const QString app = m_root.filePath("electron-app/app");
        QCOMPARE(withChromiumSwitches({app, "claude://new"}),
                 QStringList({app, "--disable-font-subpixel-positioning", "claude://new"}));
    }

    void symlinkResolvesToTheResources()
    {
        QVERIFY(isChromiumProgram(m_root.filePath("bin/app")));
    }

    void otherProgramsAreLeftAlone()
    {
        // icudtl.dat alone is not a Chromium build.
        const QStringList tool{m_root.filePath("plain/tool"), "--flag"};
        QCOMPARE(withChromiumSwitches(tool), tool);

        const QStringList missing{m_root.filePath("plain/missing")};
        QCOMPARE(withChromiumSwitches(missing), missing);
        QCOMPARE(withChromiumSwitches({}), QStringList());
    }

private:
    QTemporaryDir m_root;
};

QTEST_GUILESS_MAIN(ChromiumSwitchesTest)
#include "chromium_switches_test.moc"
