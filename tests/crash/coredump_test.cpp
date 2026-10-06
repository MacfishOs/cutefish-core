// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>

#include "coredump.h"

#include <csignal>

// As systemd-coredump 257 writes MESSAGE.
static const char s_message[] =
        "Process 2112053 (svgrender) of user 1000 dumped core.\n"
        "\n"
        "Module libstdc++.so.6 from deb gcc-14-14.2.0-19.amd64\n"
        "Stack trace of thread 2112053:\n"
        "#0  0x00007fe7192a069c n/a (libc.so.6 + 0x9469c)\n"
        "#1  0x00007fe71924bc42 raise (libc.so.6 + 0x3fc42)\n"
        "#4  0x00007fe7198dcd32 _ZNK14QMessageLogger5fatalEPKcz (libQt6Core.so.6 + 0xdcd32)\n"
        "#10 0x00005650c8d33206 main (/home/reion/align/svgrender/build/svgrender + 0x1206)\n"
        "#11 0x0000000000000000 n/a (n/a + 0x0)\n"
        "\n"
        "Stack trace of thread 2112056:\n"
        "#0  0x00007fe7192a672e n/a (libc.so.6 + 0x9a72e)\n"
        "#1  0x00007fe71931c538 n/a (libc.so.6 + 0x110538)\n"
        "ELF object binary architecture: AMD x86-64";

class CoredumpTest : public QObject
{
    Q_OBJECT

private slots:

    void parsesThreadsAndFrames()
    {
        const auto threads = Coredump::parseStackTrace(QString::fromLatin1(s_message));
        QCOMPARE(threads.size(), 2);
        QCOMPARE(threads.at(0).id, QStringLiteral("2112053"));
        QCOMPARE(threads.at(0).frames.size(), 5);
        QCOMPARE(threads.at(0).frames.at(0).function, QString());
        QCOMPARE(threads.at(0).frames.at(0).module, QStringLiteral("libc.so.6"));
        QCOMPARE(threads.at(0).frames.at(1).function, QStringLiteral("raise"));
        QCOMPARE(threads.at(0).frames.at(2).function,
                 QStringLiteral("QMessageLogger::fatal(char const*, ...) const"));
        QCOMPARE(threads.at(0).frames.at(3).module,
                 QStringLiteral("/home/reion/align/svgrender/build/svgrender"));
        QCOMPARE(threads.at(0).frames.at(4).module, QString());
        QCOMPARE(threads.at(1).frames.size(), 2);
    }

    void ignoresAMessageWithoutStack()
    {
        QVERIFY(Coredump::parseStackTrace(QStringLiteral("Process 1 (x) of user 1000 dumped core."))
                        .isEmpty());
    }

    void keepsWhatItCannotDemangle()
    {
        QCOMPARE(Coredump::demangle(QStringLiteral("_Zbogus")), QStringLiteral("_Zbogus"));
        QCOMPARE(Coredump::demangle(QStringLiteral("main")), QStringLiteral("main"));
    }

    void readsTheApplicationFromItsScope()
    {
        Coredump dump;
        dump.userUnit = QStringLiteral("app-cutefish-cutefish\\x2dsystem\\x2dmonitor-2127671.scope");
        QCOMPARE(dump.applicationId(), QStringLiteral("cutefish-system-monitor"));
        QCOMPARE(dump.applicationPid(), 2127671u);

        dump.userUnit = QStringLiteral("app-cutefish-org.gnome.\\xe6\\x96\\x87-12.scope");
        QCOMPARE(dump.applicationId(), QStringLiteral("org.gnome.文"));

        dump.userUnit = QStringLiteral("session-662.scope");
        QCOMPARE(dump.applicationId(), QString());
        QCOMPARE(dump.applicationPid(), 0u);
    }

    void writesAReport()
    {
        Coredump dump;
        dump.signal = SIGSEGV;
        dump.signalName = QStringLiteral("SIGSEGV");
        dump.executable = QStringLiteral("/usr/bin/x");
        dump.threads = Coredump::parseStackTrace(QString::fromLatin1(s_message));
        const QString report = dump.report(QStringLiteral("X"));
        QVERIFY(report.startsWith(QStringLiteral("X quit unexpectedly\nExecutable: /usr/bin/x\n")));
        QVERIFY(report.contains(QStringLiteral("(SIGSEGV)")));
        QVERIFY(report.contains(QStringLiteral("\nThread 2112053:\n#0 ?? (libc.so.6)\n#1 raise (libc.so.6)\n")));
    }
};

QTEST_GUILESS_MAIN(CoredumpTest)
#include "coredump_test.moc"
