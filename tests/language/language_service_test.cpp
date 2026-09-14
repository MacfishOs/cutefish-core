#include "locale/language.h"

#include <QDBusConnection>
#include <QTemporaryDir>
#include <QTest>

class Notifications : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    int count = 0;

public slots:
    uint Notify(const QString &, uint, const QString &, const QString &, const QString &,
                const QStringList &, const QVariantMap &, int)
    {
        return ++count;
    }
};

class LanguageServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void notificationsOnlyForPrimaryChanges()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
        Notifications notifications;
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService("org.freedesktop.Notifications"));
        QVERIFY(bus.registerObject("/org/freedesktop/Notifications", &notifications,
                                   QDBusConnection::ExportAllSlots));

        Language language;
        QSettings theme("cutefish", "theme");
        theme.setValue("Font", "Custom Font");
        language.setPreferredLanguages({"en_US", "ja_JP", "de_DE"});
        QTest::qWait(30);
        QCOMPARE(notifications.count, 0);
        QCOMPARE(theme.value("Font").toString(), "Custom Font");

        language.setPreferredLanguages({"en_US", "de_DE", "ja_JP"});
        QTest::qWait(30);
        QCOMPARE(notifications.count, 0);
        QCOMPARE(theme.value("Font").toString(), "Custom Font");
        QCOMPARE(language.preferredLanguages(), QStringList({"en_US", "de_DE", "ja_JP"}));

        language.setPreferredLanguages({"ja_JP", "en_US", "de_DE"});
        QTRY_COMPARE(notifications.count, 1);
        language.setPreferredLanguages({"ja_JP", "de_DE"});
        QTest::qWait(30);
        QCOMPARE(notifications.count, 1);
        language.setPreferredLanguages({"de_DE"});
        QTRY_COMPARE(notifications.count, 2);
        language.setPreferredLanguages({"de_DE"});
        QTest::qWait(30);
        QCOMPARE(notifications.count, 2);
        bus.unregisterObject("/org/freedesktop/Notifications");
        bus.unregisterService("org.freedesktop.Notifications");
    }
};

QTEST_GUILESS_MAIN(LanguageServiceTest)
#include "language_service_test.moc"
