// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>

#include "restartpolicy.h"

class RestartPolicyTest : public QObject
{
    Q_OBJECT

private slots:

    void givesUpAfterTheLimit()
    {
        int restarts = 0;
        RestartPolicy policy(QStringLiteral("test"), [&] { ++restarts; },
                             {3, 60000, 10, 10});
        policy.started();
        for (int i = 0; i < 3; ++i) {
            policy.exited(1, QProcess::CrashExit);
            QVERIFY(policy.isPending());
            QTRY_COMPARE(restarts, i + 1);
            policy.started();
        }
        policy.exited(1, QProcess::CrashExit);
        QVERIFY(!policy.isPending());
        QVERIFY(policy.hasGivenUp());
    }

    void backsOff()
    {
        int restarts = 0;
        RestartPolicy policy(QStringLiteral("test"), [&] { ++restarts; },
                             {5, 60000, 50, 200});
        policy.started();
        policy.exited(1, QProcess::CrashExit);
        QTRY_COMPARE(restarts, 1);
        policy.started();
        policy.exited(1, QProcess::CrashExit);
        QTest::qWait(150);
        QCOMPARE(restarts, 1);
        QTRY_COMPARE(restarts, 2);
    }

    void stableUptimeResetsTheCount()
    {
        int restarts = 0;
        RestartPolicy policy(QStringLiteral("test"), [&] { ++restarts; },
                             {1, 100, 10, 10});
        policy.started();
        policy.exited(1, QProcess::CrashExit);
        QTRY_COMPARE(restarts, 1);
        policy.started();
        QTest::qWait(150);
        policy.exited(1, QProcess::CrashExit);
        QTRY_COMPARE(restarts, 2);
        QVERIFY(!policy.hasGivenUp());
    }

    void stopCancelsAPendingRestart()
    {
        int restarts = 0;
        RestartPolicy policy(QStringLiteral("test"), [&] { ++restarts; },
                             {5, 60000, 50, 10});
        policy.started();
        policy.exited(0, QProcess::NormalExit);
        policy.stop();
        QTest::qWait(100);
        QCOMPARE(restarts, 0);
        policy.exited(0, QProcess::NormalExit);
        QVERIFY(!policy.isPending());
    }
};

QTEST_GUILESS_MAIN(RestartPolicyTest)
#include "restart_policy_test.moc"
