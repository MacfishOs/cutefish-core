#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

#include <gtest/gtest.h>

#include "base/process.h"
#include "common/environment.h"
#include "shellconnection.h"
#include "shellprocess.h"
#include "testutil.h"

using cutefish::base::EventLoop;
using sessiontest::runUntil;

static std::string selfPath()
{
    char path[4096];
    const ssize_t length = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
    return length > 0 ? std::string(path, size_t(length)) : std::string();
}

static std::string controlSocketPath()
{
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    return std::string(runtime && *runtime ? runtime : "/tmp") + "/cutefish-session-lifecycle-test.control";
}

static bool hasVariable(const char* name)
{
    return std::getenv(name) != nullptr;
}

static std::string variable(const char* name)
{
    const char* value = std::getenv(name);
    return value ? value : "";
}

namespace {

// A stand-in for the compositor's control channel: accepts one connection and
// collects the descriptors sent over it.
class FakeCompositor
{
public:
    bool listen(const std::string& path)
    {
        m_path = path;
        ::unlink(path.c_str());

        sockaddr_un address {};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.data(), path.size());

        m_listenFd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (m_listenFd < 0)
            return false;
        if (bind(m_listenFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
            return false;
        return ::listen(m_listenFd, 4) == 0;
    }

    bool accept()
    {
        m_clientFd = accept4(m_listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        return m_clientFd >= 0;
    }

    //! Blocks for the next descriptor, or -1.
    int receiveFd()
    {
        char payload = 0;
        iovec iov {};
        iov.iov_base = &payload;
        iov.iov_len = 1;

        union {
            char buffer[CMSG_SPACE(sizeof(int))];
            cmsghdr alignment;
        } control {};

        msghdr message {};
        message.msg_iov = &iov;
        message.msg_iovlen = 1;
        message.msg_control = control.buffer;
        message.msg_controllen = sizeof(control.buffer);

        if (recvmsg(m_clientFd, &message, MSG_CMSG_CLOEXEC) <= 0)
            return -1;

        for (cmsghdr* header = CMSG_FIRSTHDR(&message); header;
             header = CMSG_NXTHDR(&message, header)) {
            if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS) {
                int fd = -1;
                std::memcpy(&fd, CMSG_DATA(header), sizeof(fd));
                return fd;
            }
        }
        return -1;
    }

    ~FakeCompositor()
    {
        if (m_clientFd >= 0)
            ::close(m_clientFd);
        if (m_listenFd >= 0)
            ::close(m_listenFd);
        if (!m_path.empty())
            ::unlink(m_path.c_str());
    }

private:
    std::string m_path;
    int m_listenFd = -1;
    int m_clientFd = -1;
};

struct Finished
{
    int count = 0;
    int code = -1;
    bool crashed = false;
};

} // namespace

TEST(ShellLifecycle, environmentAndSingleStart)
{
    EventLoop loop;
    setenv("SHELL_TEST_CHILD", "1", 1);
    ShellProcess process;
    Finished finished;
    process.onFinished = [&finished](int code, bool crashed) {
        ++finished.count;
        finished.code = code;
        finished.crashed = crashed;
    };
    EXPECT_TRUE(process.startForSession(selfPath(), "shell-test-socket", nullptr));
    EXPECT_FALSE(process.startForSession("/does-not-exist/duplicate-shell", "wrong-socket", nullptr));
    ASSERT_TRUE(runUntil(loop, [&] { return finished.count == 1; }));
    EXPECT_FALSE(finished.crashed);
    EXPECT_EQ(finished.code, 0);
}

TEST(ShellLifecycle, stopAndMissingExecutable)
{
    EventLoop loop;
    setenv("SHELL_TEST_CHILD", "hang", 1);
    ShellProcess process;
    EXPECT_TRUE(process.startForSession(selfPath(), "shell-test-socket", nullptr));
    EXPECT_TRUE(process.isRunning());
    process.stop();
    EXPECT_FALSE(process.isRunning());
    process.stop();

    EXPECT_FALSE(process.startForSession("/does-not-exist/cutefish-filemanager", "shell-test-socket", nullptr));
    EXPECT_FALSE(process.isRunning());
}

// The socketpair the session creates really reaches both sides: the
// compositor gets one end over SCM_RIGHTS, the shell the other.
TEST(ShellLifecycle, theCompositorReceivesTheShellConnection)
{
    FakeCompositor compositor;
    const std::string path = controlSocketPath();
    ASSERT_TRUE(compositor.listen(path));

    ShellConnection connection;
    ASSERT_TRUE(connection.connectToCompositor(path, 2000));
    ASSERT_TRUE(compositor.accept());

    const int shellEnd = connection.createShellConnection();
    ASSERT_GE(shellEnd, 0);

    const int compositorEnd = compositor.receiveFd();
    ASSERT_GE(compositorEnd, 0);

    // The two descriptors are the two ends of one socket.
    EXPECT_EQ(::write(shellEnd, "x", 1), ssize_t(1));
    char received = 0;
    EXPECT_EQ(::read(compositorEnd, &received, 1), ssize_t(1));
    EXPECT_EQ(received, 'x');

    ::close(shellEnd);
    ::close(compositorEnd);
}

// The daemon is started with a usable --desktop-socket descriptor and the
// public WAYLAND_DISPLAY, and neither the descriptor nor WAYLAND_SOCKET
// reaches anything it launches.
TEST(ShellLifecycle, thePrivilegedConnectionDoesNotLeakToChildren)
{
    EventLoop loop;
    FakeCompositor compositor;
    const std::string path = controlSocketPath();
    ASSERT_TRUE(compositor.listen(path));

    ShellConnection connection;
    ASSERT_TRUE(connection.connectToCompositor(path, 2000));
    ASSERT_TRUE(compositor.accept());

    setenv("SHELL_TEST_CHILD", "privileged", 1);
    ShellProcess process;
    Finished finished;
    process.onFinished = [&finished](int code, bool crashed) {
        ++finished.count;
        finished.code = code;
        finished.crashed = crashed;
    };
    ASSERT_TRUE(process.startForSession(selfPath(), "shell-test-socket", &connection));
    EXPECT_GE(compositor.receiveFd(), 0);
    ASSERT_TRUE(runUntil(loop, [&] { return finished.count == 1; }, 15000));
    EXPECT_FALSE(finished.crashed);
    // The child checks its own environment and its grandchild's, and exits
    // non-zero on the first thing that leaked.
    EXPECT_EQ(finished.code, 0);
}

// Runs as the file manager daemon: verifies its own privileged command line,
// then starts a grandchild the way the application runtime starts an
// application and verifies that nothing privileged came along.
static int runPrivilegedChild(int argc, char** argv)
{
    if (variable("WAYLAND_DISPLAY") != "shell-test-socket")
        return 3;
    if (hasVariable("WAYLAND_SOCKET"))
        return 4;

    const std::vector<std::string> arguments(argv + 1, argv + argc);
    if (arguments.size() != 3 || arguments.at(0) != "--daemon" || arguments.at(1) != "--desktop-socket")
        return 5;
    char* end = nullptr;
    const long fd = std::strtol(arguments.at(2).c_str(), &end, 10);
    if (*end || fd < 0)
        return 5;
    // The descriptor really was inherited and really is a socket.
    if (fcntl(int(fd), F_GETFD) < 0)
        return 6;

    // What the daemon does once its trusted connection has the descriptor.
    if (fcntl(int(fd), F_SETFD, FD_CLOEXEC) < 0)
        return 7;

    EventLoop loop;
    cutefish::base::Process grandchild(&loop);
    cutefish::core::Environment environment = cutefish::core::processEnvironment();
    environment.erase("WAYLAND_SOCKET");
    environment["SHELL_TEST_CHILD"] = "grandchild";
    environment["SHELL_TEST_PRIVILEGED_FD"] = std::to_string(fd);
    grandchild.setEnvironment(cutefish::core::environmentEntries(environment));
    int exitCode = 8;
    grandchild.onFinished = [&exitCode](int code, bool) {
        exitCode = code;
    };
    if (!grandchild.start(selfPath()) || !grandchild.waitForFinished(std::chrono::seconds(10)))
        return 8;

    return exitCode;
}

// Runs as an application the shell launched.
static int runGrandchild()
{
    if (hasVariable("WAYLAND_SOCKET"))
        return 20;
    if (variable("WAYLAND_DISPLAY") != "shell-test-socket")
        return 21;

    // The privileged descriptor must not have been inherited.
    const int fd = std::atoi(variable("SHELL_TEST_PRIVILEGED_FD").c_str());
    if (fd > 0 && fcntl(fd, F_GETFD) >= 0)
        return 22;

    return 0;
}

int main(int argc, char** argv)
{
    const std::string mode = variable("SHELL_TEST_CHILD");

    if (mode == "grandchild")
        return runGrandchild();

    if (mode == "privileged")
        return runPrivilegedChild(argc, argv);

    if (!mode.empty()) {
        // Without a connection: the daemon alone, no descriptor.
        if (variable("WAYLAND_DISPLAY") != "shell-test-socket" || variable("DISPLAY") != ":99"
            || variable("XAUTHORITY") != "/test-authority" || hasVariable("WAYLAND_SOCKET")
            || argc != 2 || std::string(argv[1]) != "--daemon")
            return 2;
        if (mode != "hang") {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return 0;
        }
        for (;;)
            pause();
    }

    setenv("DISPLAY", ":99", 1);
    setenv("XAUTHORITY", "/test-authority", 1);
    setenv("WAYLAND_SOCKET", "999", 1);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
