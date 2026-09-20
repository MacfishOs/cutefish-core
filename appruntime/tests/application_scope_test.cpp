#include "applicationscope.h"

#include <QTest>

class ApplicationScopeTest : public QObject
{
    Q_OBJECT

private slots:
    void namesFollowTheDesktopEntrySpecification()
    {
        QCOMPARE(ApplicationScope::unitName("com.obsproject.Studio", 4242),
                 "app-cutefish-com.obsproject.Studio-4242.scope");
        // Dashes must not be mistaken for the launcher and random separators.
        QCOMPARE(ApplicationScope::unitName("cutefish-filemanager", 7),
                 "app-cutefish-cutefish\\x2dfilemanager-7.scope");
        QCOMPARE(ApplicationScope::unitName(".hidden app", 1),
                 "app-cutefish-\\x2ehidden\\x20app-1.scope");
        QCOMPARE(ApplicationScope::unitName("org.gnome.Evolution-alarm-notify", 2437),
                 "app-cutefish-org.gnome.Evolution\\x2dalarm\\x2dnotify-2437.scope");
    }
};

QTEST_GUILESS_MAIN(ApplicationScopeTest)
#include "application_scope_test.moc"
