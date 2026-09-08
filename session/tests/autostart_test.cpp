#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "autostartentries.h"

class AutostartTest : public QObject
{
    Q_OBJECT
    static void entry(const QString& dir, const QString& name, const QByteArray& values)
    {
        QVERIFY(QDir().mkpath(dir));
        QFile file(dir + '/' + name + ".desktop");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Desktop Entry]\nType=Application\n" + values);
    }

private slots:

    void overrides()
    {
        QTemporaryDir root;
        const auto user = root.filePath("user");
        const auto system = root.filePath("system");
        entry(system, "bridge", "Exec=bridge\n");
        entry(user, "bridge", "Hidden=true\n");
        entry(system, "duplicate", "Exec=old\n");
        entry(user, "duplicate", "Exec=new\n");
        entry(system, "filtered", "Exec=wrong-fallback\n");
        entry(user, "filtered", "Exec=other\nOnlyShowIn=KDE;\n");
        QCOMPARE(autostartEntries({user, system, system}, {"Cutefish"}), QStringList {"new"});
    }

    void filtering()
    {
        QTemporaryDir root;
        entry(root.path(), "allowed", "Exec=allowed\nOnlyShowIn=KDE;Cutefish;\n");
        entry(root.path(), "excluded", "Exec=excluded\nNotShowIn=Cutefish;\n");
        entry(root.path(), "other", "Exec=other\nOnlyShowIn=KDE;\n");
        entry(root.path(), "hidden", "Exec=hidden\nHidden=true\n");
        entry(root.path(), "menu-hidden", "Exec=menu-hidden\nNoDisplay=true\n");
        entry(root.path(), "visible", "Exec=visible\nHidden=false\n");
        QCOMPARE(autostartEntries({root.path()}, {"Cutefish"}),
                 (QStringList {"allowed", "menu-hidden", "visible"}));
        QVERIFY(autostartEntries({root.path()}, {"Other", "KDE"}).contains("other"));
    }

    void tryExec()
    {
        QTemporaryDir root;
        const auto program = root.filePath("executable");
        QFile file(program);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        entry(root.path(), "absolute", "Exec=absolute\nTryExec=" + program.toUtf8() + "\n");
        entry(root.path(), "missing", "Exec=missing\nTryExec=cutefish-nonexistent-test-program\n");
        entry(root.path(), "relative", "Exec=relative\nTryExec=./executable\n");
        QVERIFY(autostartEntries({root.path()}, {"Cutefish"}).isEmpty());
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QCOMPARE(autostartEntries({root.path()}, {"Cutefish"}), QStringList {"absolute"});
        entry(root.path(), "path", "Exec=path\nTryExec=sh\n");
        QVERIFY(autostartEntries({root.path()}, {"Cutefish"}).contains("path"));
    }
};
QTEST_GUILESS_MAIN(AutostartTest)
#include "autostart_test.moc"
