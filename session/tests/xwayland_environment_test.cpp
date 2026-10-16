#include <cstdlib>
#include <filesystem>
#include <sys/stat.h>

#include <gtest/gtest.h>

#include "base/file.h"
#include "base/json.h"
#include "base/process.h"
#include "common/environment.h"
#include "testutil.h"
#include "xwaylandenvironment.h"

using cutefish::base::EventLoop;
using cutefish::base::file::TemporaryDirectory;
using sessiontest::runUntil;

namespace json = cutefish::base::json;

static void publish(const std::string& path, const std::string& display, const std::string& authority)
{
    json::Value document = json::Value::object();
    document.insert("DISPLAY", display);
    document.insert("XAUTHORITY", authority);
    ASSERT_TRUE(cutefish::base::file::writeAtomically(path, document.serialize(false)));
}

static bool isEmptyVariable(const char* name)
{
    const char* value = std::getenv(name);
    return !value || !*value;
}

static std::string variable(const char* name)
{
    const char* value = std::getenv(name);
    return value ? value : "";
}

static bool createAuthority(const std::string& path, mode_t mode)
{
    if (!sessiontest::writeFile(path, ""))
        return false;
    return ::chmod(path.c_str(), mode) == 0;
}

struct Resolved
{
    std::vector<bool> results;
    int lost = 0;
};

static void track(XwaylandEnvironment& environment, Resolved& resolved)
{
    environment.onResolved = [&resolved](bool available) { resolved.results.push_back(available); };
    environment.onLost = [&resolved] { ++resolved.lost; };
}

TEST(XwaylandEnvironment, arrivalOrdersAndLoss)
{
    for (bool early : {false, true}) {
        EventLoop loop;
        TemporaryDirectory directory(sessiontest::temporaryParent());
        const std::string authority = directory.path() + "/auth";
        ASSERT_TRUE(createAuthority(authority, 0600));
        XwaylandEnvironment environment;
        Resolved resolved;
        track(environment, resolved);
        setenv("DISPLAY", ":old", 1);
        setenv("XAUTHORITY", "/old", 1);
        if (early)
            publish(environment.readinessFile(), ":123", authority);
        environment.waitForReady(1000);
        if (!early) {
            EXPECT_TRUE(isEmptyVariable("DISPLAY"));
            publish(environment.readinessFile(), ":123", authority);
        }
        ASSERT_TRUE(runUntil(loop, [&] { return resolved.results.size() == 1; }, 1500));
        EXPECT_TRUE(resolved.results.front());
        EXPECT_EQ(variable("DISPLAY"), ":123");
        EXPECT_EQ(variable("XAUTHORITY"), authority);
        ASSERT_TRUE(std::filesystem::remove(environment.readinessFile()));
        ASSERT_TRUE(runUntil(loop, [&] { return resolved.lost == 1; }, 1500));
        EXPECT_TRUE(isEmptyVariable("DISPLAY"));
        EXPECT_TRUE(isEmptyVariable("XAUTHORITY"));
    }
}

TEST(XwaylandEnvironment, malformedAndTimeout)
{
    EventLoop loop;
    XwaylandEnvironment environment;
    Resolved resolved;
    track(environment, resolved);
    publish(environment.readinessFile(), "remote:0", "/missing");
    environment.waitForReady(30);
    ASSERT_TRUE(runUntil(loop, [&] { return resolved.results.size() == 1; }, 1000));
    EXPECT_FALSE(resolved.results.front());
    EXPECT_TRUE(isEmptyVariable("DISPLAY"));
}

static std::string testCompositor()
{
    return variable("CUTEFISH_TEST_COMPOSITOR");
}

TEST(XwaylandEnvironment, realCompositor)
{
    const std::string binary = testCompositor();
    if (binary.empty())
        GTEST_SKIP() << "Set CUTEFISH_TEST_COMPOSITOR for real Xwayland integration";
    EventLoop loop;
    TemporaryDirectory runtime(sessiontest::temporaryParent());
    XwaylandEnvironment environment;
    Resolved resolved;
    track(environment, resolved);
    cutefish::base::Process compositor(&loop);
    cutefish::core::Environment childEnvironment = cutefish::core::processEnvironment();
    childEnvironment["XDG_RUNTIME_DIR"] = runtime.path();
    compositor.setEnvironment(cutefish::core::environmentEntries(childEnvironment));
    ASSERT_TRUE(compositor.start(binary, {"--no-drm", "--socket", "session-test", "--xwayland-shm",
                                          "--xwayland-ready-file", environment.readinessFile()}));
    environment.waitForReady(10000);
    ASSERT_TRUE(runUntil(loop, [&] { return resolved.results.size() == 1; }, 12000));
    EXPECT_TRUE(resolved.results.front()) << compositor.standardError();
    cutefish::base::Process client(&loop);
    int clientCode = -1;
    client.onFinished = [&clientCode](int code, bool) { clientCode = code; };
    ASSERT_TRUE(client.start("xdpyinfo"));
    EXPECT_TRUE(client.waitForFinished(std::chrono::seconds(3)));
    EXPECT_EQ(clientCode, 0);
    int compositorCode = -1;
    compositor.onFinished = [&compositorCode](int code, bool) { compositorCode = code; };
    compositor.terminate();
    EXPECT_TRUE(compositor.waitForFinished(std::chrono::seconds(5)));
    EXPECT_EQ(compositorCode, 0);
}

TEST(XwaylandEnvironment, disabledXwaylandKeepsNativeCompositor)
{
    const std::string binary = testCompositor();
    if (binary.empty())
        GTEST_SKIP() << "Set CUTEFISH_TEST_COMPOSITOR for real integration";
    EventLoop loop;
    TemporaryDirectory runtime(sessiontest::temporaryParent());
    XwaylandEnvironment environment;
    Resolved resolved;
    track(environment, resolved);
    cutefish::base::Process compositor(&loop);
    cutefish::core::Environment childEnvironment = cutefish::core::processEnvironment();
    childEnvironment["XDG_RUNTIME_DIR"] = runtime.path();
    compositor.setEnvironment(cutefish::core::environmentEntries(childEnvironment));
    ASSERT_TRUE(compositor.start(binary, {"--no-drm", "--socket", "session-test", "--no-xwayland",
                                          "--xwayland-ready-file", environment.readinessFile()}));
    ASSERT_TRUE(runUntil(loop, [&] { return std::filesystem::exists(runtime.path() + "/session-test"); }, 3000));
    environment.waitForReady(30);
    ASSERT_TRUE(runUntil(loop, [&] { return resolved.results.size() == 1; }, 1000));
    EXPECT_FALSE(resolved.results.front());
    EXPECT_TRUE(compositor.isRunning());
    int compositorCode = -1;
    compositor.onFinished = [&compositorCode](int code, bool) { compositorCode = code; };
    compositor.terminate();
    EXPECT_TRUE(compositor.waitForFinished(std::chrono::seconds(5)));
    EXPECT_EQ(compositorCode, 0);
}

TEST(XwaylandEnvironment, unsafeAuthority)
{
    EventLoop loop;
    TemporaryDirectory directory(sessiontest::temporaryParent());
    const std::string authority = directory.path() + "/auth";
    ASSERT_TRUE(createAuthority(authority, 0604));
    XwaylandEnvironment environment;
    Resolved resolved;
    track(environment, resolved);
    publish(environment.readinessFile(), ":123", authority);
    environment.waitForReady(30);
    ASSERT_TRUE(runUntil(loop, [&] { return resolved.results.size() == 1; }, 1000));
    EXPECT_FALSE(resolved.results.front());
}
