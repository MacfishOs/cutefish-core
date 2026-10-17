#include <algorithm>
#include <sys/stat.h>

#include <gtest/gtest.h>

#include "autostartentries.h"
#include "base/file.h"
#include "testutil.h"

using cutefish::base::file::TemporaryDirectory;
using Entries = std::vector<std::string>;

static void entry(const std::string& dir, const std::string& name, const std::string& values)
{
    ASSERT_TRUE(sessiontest::writeFile(dir + '/' + name + ".desktop",
                                       "[Desktop Entry]\nType=Application\n" + values));
}

static bool contains(const Entries& entries, const std::string& value)
{
    return std::find(entries.begin(), entries.end(), value) != entries.end();
}

TEST(Autostart, overrides)
{
    TemporaryDirectory root(sessiontest::temporaryParent());
    const auto user = root.path() + "/user";
    const auto system = root.path() + "/system";
    entry(system, "bridge", "Exec=bridge\n");
    entry(user, "bridge", "Hidden=true\n");
    entry(system, "switched-off", "Exec=switched-off\n");
    entry(user, "switched-off", "Exec=switched-off\nX-GNOME-Autostart-enabled=false\n");
    entry(system, "duplicate", "Exec=old\n");
    entry(user, "duplicate", "Exec=new\n");
    entry(system, "filtered", "Exec=wrong-fallback\n");
    entry(user, "filtered", "Exec=other\nOnlyShowIn=KDE;\n");
    EXPECT_EQ(autostartEntries({user, system, system}, {"Cutefish"}), Entries {"new"});
}

TEST(Autostart, filtering)
{
    TemporaryDirectory root(sessiontest::temporaryParent());
    entry(root.path(), "allowed", "Exec=allowed\nOnlyShowIn=KDE;Cutefish;\n");
    entry(root.path(), "excluded", "Exec=excluded\nNotShowIn=Cutefish;\n");
    entry(root.path(), "other", "Exec=other\nOnlyShowIn=KDE;\n");
    entry(root.path(), "hidden", "Exec=hidden\nHidden=true\n");
    entry(root.path(), "menu-hidden", "Exec=menu-hidden\nNoDisplay=true\n");
    entry(root.path(), "visible", "Exec=visible\nHidden=false\n");
    entry(root.path(), "disabled", "Exec=disabled\nX-GNOME-Autostart-enabled=false\n");
    entry(root.path(), "enabled", "Exec=enabled\nX-GNOME-Autostart-enabled=true\n");
    EXPECT_EQ(autostartEntries({root.path()}, {"Cutefish"}),
              (Entries {"allowed", "enabled", "menu-hidden", "visible"}));
    EXPECT_TRUE(contains(autostartEntries({root.path()}, {"Other", "KDE"}), "other"));
}

TEST(Autostart, tryExec)
{
    TemporaryDirectory root(sessiontest::temporaryParent());
    const auto program = root.path() + "/executable";
    ASSERT_TRUE(sessiontest::writeFile(program, ""));
    ASSERT_EQ(::chmod(program.c_str(), 0600), 0);
    entry(root.path(), "absolute", "Exec=absolute\nTryExec=" + program + "\n");
    entry(root.path(), "missing", "Exec=missing\nTryExec=cutefish-nonexistent-test-program\n");
    entry(root.path(), "relative", "Exec=relative\nTryExec=./executable\n");
    EXPECT_TRUE(autostartEntries({root.path()}, {"Cutefish"}).empty());
    ASSERT_EQ(::chmod(program.c_str(), 0700), 0);
    EXPECT_EQ(autostartEntries({root.path()}, {"Cutefish"}), Entries {"absolute"});
    entry(root.path(), "path", "Exec=path\nTryExec=sh\n");
    EXPECT_TRUE(contains(autostartEntries({root.path()}, {"Cutefish"}), "path"));
}
