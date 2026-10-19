// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include <chrono>
#include <filesystem>
#include <sys/stat.h>
#include <syslog.h>
#include <thread>
#include <unistd.h>

#include <gtest/gtest.h>
#include <systemd/sd-journal.h>

#include "base/file.h"
#include "base/process.h"
#include "journalstream.h"
#include "testutil.h"

using cutefish::base::EventLoop;
using cutefish::base::Process;
using cutefish::base::file::TemporaryDirectory;
using sessiontest::runUntil;

static ino_t inode(int fd)
{
    struct stat status {};
    return ::fstat(fd, &status) == 0 ? status.st_ino : 0;
}

static std::string journalLines(EventLoop &loop, const std::string &identifier)
{
    Process journal(&loop);
    bool finished = false;
    journal.onFinished = [&](int, bool) { finished = true; };
    if (!journal.start("journalctl", {"--quiet", "--output=cat", "--identifier=" + identifier}))
        return {};
    runUntil(loop, [&] { return finished; });
    return journal.standardOutput();
}

TEST(JournalStream, childWritesUnderItsProgramName)
{
    EventLoop loop;
    const int stream = sd_journal_stream_fd("cutefish-session-test", LOG_INFO, 1);
    if (stream < 0)
        GTEST_SKIP() << "no journal";

    TemporaryDirectory root(sessiontest::temporaryParent());
    const std::string name = "cutefish-journal-stream-test-" + std::to_string(::getpid());
    const std::string program = root.path() + '/' + name;
    std::filesystem::create_symlink("/bin/sh", program);

    const int errors = ::dup(STDERR_FILENO);
    ASSERT_GE(errors, 0);
    ASSERT_EQ(::dup2(stream, STDERR_FILENO), STDERR_FILENO);
    const ino_t ownStream = inode(STDERR_FILENO);

    Process child(&loop);
    child.setOutputForwarded(true);
    bool finished = false;
    child.onFinished = [&](int, bool) { finished = true; };
    const bool started = startWithOwnJournalStream(child, program, {"-c", "echo output; echo errors >&2"});
    const ino_t afterStart = inode(STDERR_FILENO);

    ::dup2(errors, STDERR_FILENO);
    ::close(errors);
    ::close(stream);

    ASSERT_TRUE(started);
    EXPECT_EQ(afterStart, ownStream);
    ASSERT_TRUE(runUntil(loop, [&] { return finished; }));

    std::string lines;
    for (int attempt = 0; attempt < 50 && lines != "output\nerrors\n"; ++attempt) {
        lines = journalLines(loop, name);
        if (lines.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    EXPECT_EQ(lines, "output\nerrors\n");
}
