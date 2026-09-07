#include <gtest/gtest.h>

#include "sysmon/platform.hpp"

#include <chrono>
#include <cctype>
#include <string>
#include <vector>

#if defined(SYSMON_POSIX)
#  include <cerrno>
#  include <csignal>
#  include <sys/ioctl.h>
#  include <sys/select.h>
#  include <sys/wait.h>
#  include <termios.h>
#  include <unistd.h>
#  if defined(SYSMON_MACOS)
#    include <util.h>
#  else
#    include <pty.h>
#  endif
#endif

// The live dashboard can only be exercised against a real terminal, so these
// tests run sysmon under a pseudo-terminal.
//
// The regression they guard is subtle: stdin and stdout are the *same* open
// file description in a terminal, so putting stdin into O_NONBLOCK also makes
// stdout non-blocking. The first time the terminal's output buffer filled,
// std::cout's write returned EAGAIN, the stream latched its error state, and
// every later frame was silently discarded — the dashboard painted about one
// kilobyte and then went blank forever.

#if defined(SYSMON_POSIX)

namespace {

struct PtyRun {
    std::string output;
    int  frames{0};
    bool exited{false};
};

/// Run the sysmon binary under a pty of the given size for a few seconds.
PtyRun run_in_pty(const std::vector<std::string>& args, int columns, int rows,
                  double seconds) {
    PtyRun result;

    int master = -1;
    struct winsize ws{};
    ws.ws_col = static_cast<unsigned short>(columns);
    ws.ws_row = static_cast<unsigned short>(rows);

    const pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0) return result;

    if (pid == 0) {
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(SYSMON_BINARY_PATH));
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execv(SYSMON_BINARY_PATH, argv.data());
        _exit(127);
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(static_cast<int>(seconds * 1000));
    while (std::chrono::steady_clock::now() < deadline) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(master, &fds);
        struct timeval tv{0, 200000};

        if (select(master + 1, &fds, nullptr, nullptr, &tv) > 0) {
            char buffer[65536];
            const ssize_t n = ::read(master, buffer, sizeof(buffer));
            if (n <= 0) break;
            result.output.append(buffer, static_cast<size_t>(n));
        }
    }

    // Close the master first: that hangs up the slave, so a child blocked
    // writing into a full terminal buffer is released rather than sitting there
    // while we wait for it.
    close(master);
    kill(pid, SIGKILL);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        // retry
    }
    result.exited = true;

    // Each frame begins by homing the cursor.
    for (size_t pos = result.output.find("\033[H"); pos != std::string::npos;
         pos = result.output.find("\033[H", pos + 1)) {
        ++result.frames;
    }
    return result;
}

} // namespace

TEST(TuiPtyTest, KeepsPaintingAfterTheTerminalBufferFills) {
    // Two seconds at a one second interval must produce several frames and far
    // more than one buffer's worth of output. Before the fix this stopped at
    // roughly 1 KB and one partial frame.
    const PtyRun run = run_in_pty({"--interval", "1"}, 120, 45, 3.0);

    EXPECT_GT(run.output.size(), 20000u)
        << "output stalled after " << run.output.size() << " bytes";
    EXPECT_GE(run.frames, 2) << "only " << run.frames << " frame(s) painted";
}

TEST(TuiPtyTest, EntersAndLeavesTheAlternateScreen) {
    const PtyRun run = run_in_pty({"--interval", "1"}, 100, 40, 1.5);
    EXPECT_NE(run.output.find("\033[?1049h"), std::string::npos)
        << "alternate screen buffer was never entered";
    EXPECT_NE(run.output.find("\033[?25l"), std::string::npos)
        << "cursor was never hidden";
}

TEST(TuiPtyTest, RendersEverySectionHeading) {
    const PtyRun run = run_in_pty({"--interval", "1"}, 120, 60, 2.5);
    for (const char* heading : {"CPU", "Memory", "Load Average", "Top Processes"}) {
        EXPECT_NE(run.output.find(heading), std::string::npos)
            << "missing section: " << heading;
    }
}

TEST(TuiPtyTest, NoLineExceedsTheTerminalWidth) {
    // Column overflow is what wraps the dashboard and destroys the layout.
    for (int columns : {60, 80, 100, 140}) {
        const PtyRun run = run_in_pty({"--interval", "1", "--limit", "5"}, columns, 60, 2.5);
        ASSERT_FALSE(run.output.empty()) << "no output at " << columns << " columns";

        // Take the last complete frame and strip the escape sequences.
        const size_t last = run.output.rfind("\033[H");
        ASSERT_NE(last, std::string::npos);
        const std::string frame = run.output.substr(last);

        std::string plain;
        for (size_t i = 0; i < frame.size(); ++i) {
            if (frame[i] == '\033' && i + 1 < frame.size() && frame[i + 1] == '[') {
                i += 2;
                while (i < frame.size() && !std::isalpha(static_cast<unsigned char>(frame[i]))) ++i;
                continue;
            }
            if (frame[i] == '\r') continue;
            plain += frame[i];
        }

        // Count display columns, treating a UTF-8 lead byte as one column and
        // continuation bytes as zero.
        size_t width = 0;
        int line = 0;
        for (char c : plain) {
            if (c == '\n') { width = 0; ++line; continue; }
            if ((static_cast<unsigned char>(c) & 0xC0) == 0x80) continue;  // continuation
            ++width;
            EXPECT_LE(width, static_cast<size_t>(columns))
                << "line " << line << " overflows a " << columns << "-column terminal";
            if (width > static_cast<size_t>(columns)) break;
        }
    }
}

#else

TEST(TuiPtyTest, SkippedOnThisPlatform) {
    GTEST_SKIP() << "pty-based tests require POSIX; the Windows console path is "
                    "covered by the CI smoke tests instead";
}

#endif // SYSMON_POSIX
