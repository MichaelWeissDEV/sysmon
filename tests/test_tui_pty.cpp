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

/// One batch of keystrokes to type into the dashboard, and when.
struct KeyScript {
    double      after_seconds{1.0};  ///< Wait this long before typing
    std::string keys;                ///< Raw bytes, escape sequences included
};

/// Run the sysmon binary under a pty of the given size for a few seconds,
/// optionally typing at it.
PtyRun run_in_pty(const std::vector<std::string>& args, int columns, int rows,
                  double seconds, const std::vector<KeyScript>& script = {}) {
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

    const auto start    = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::milliseconds(static_cast<int>(seconds * 1000));
    size_t next_script  = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        // Type the next batch once its moment has come.  Writing to the pty
        // master is exactly what a user pressing a key does.
        if (next_script < script.size()) {
            const auto due = start + std::chrono::milliseconds(
                static_cast<int>(script[next_script].after_seconds * 1000));
            if (std::chrono::steady_clock::now() >= due) {
                const std::string& keys = script[next_script].keys;
                if (::write(master, keys.data(), keys.size()) < 0) {
                    // The child is gone; nothing more to type.
                    next_script = script.size();
                } else {
                    ++next_script;
                }
            }
        }

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

namespace {

/// Strip ANSI escapes from a frame, leaving the characters a terminal prints.
std::string strip_escapes(const std::string& frame) {
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
    return plain;
}

/// The last complete frame, with its escape sequences removed.
///
/// Searching the whole capture would also match the overview frames painted
/// before a key was pressed, and "Processes", "GPU" and "Connections" all
/// appear there — so a marker check against the full output asserts nothing
/// about whether the focus view actually opened.
std::string last_frame(const std::string& output) {
    const size_t last = output.rfind("\033[H");
    if (last == std::string::npos) return "";
    return strip_escapes(output.substr(last));
}

/// The widest line of the last complete frame, in display columns.
///
/// A UTF-8 lead byte counts as one column and continuation bytes as zero,
/// which is what the box-drawing and arrow glyphs in the dashboard need.
size_t widest_line(const std::string& output, int* worst_line = nullptr) {
    const size_t last = output.rfind("\033[H");
    if (last == std::string::npos) return 0;
    const std::string plain = strip_escapes(output.substr(last));

    size_t width = 0, widest = 0;
    int line = 0, widest_at = 0;
    for (char c : plain) {
        if (c == '\n') { width = 0; ++line; continue; }
        if ((static_cast<unsigned char>(c) & 0xC0) == 0x80) continue;
        ++width;
        if (width > widest) { widest = width; widest_at = line; }
    }
    if (worst_line != nullptr) *worst_line = widest_at;
    return widest;
}

} // namespace

TEST(TuiPtyTest, NoLineExceedsTheTerminalWidth) {
    // Column overflow is what wraps the dashboard and destroys the layout.
    for (int columns : {60, 80, 100, 140}) {
        const PtyRun run = run_in_pty({"--interval", "1", "--limit", "5"}, columns, 60, 2.5);
        ASSERT_FALSE(run.output.empty()) << "no output at " << columns << " columns";

        int worst = -1;
        EXPECT_LE(widest_line(run.output, &worst), static_cast<size_t>(columns))
            << "line " << worst << " overflows a " << columns << "-column terminal";
    }
}

// ---------------------------------------------------------------------------
// Views and navigation
// ---------------------------------------------------------------------------

TEST(TuiPtyTest, ArrowKeysDoNotQuitTheDashboard) {
    // Every arrow key starts with the same byte as Escape, and Escape used to
    // be wired straight to "quit".  Pressing Down therefore killed the
    // dashboard; the decoder's timed lookahead is what tells them apart.
    const PtyRun run = run_in_pty({"--interval", "1"}, 100, 40, 3.0,
                                  {{1.0, "7"},                       // process view
                                   {1.5, "\033[B\033[B\033[B"},      // three Downs
                                   {2.0, "\033[A"}});                // and an Up

    EXPECT_GE(run.frames, 3) << "dashboard stopped painting after an arrow key";
    EXPECT_GT(run.output.size(), 20000u);
}

TEST(TuiPtyTest, EveryFocusViewRendersWithinTheTerminalWidth) {
    // Each view is its own layout, so each one can overflow on its own.
    struct Case { const char* key; const char* marker; };
    static const Case cases[] = {
        {"1", "Processor"},   {"2", "Physical memory"}, {"3", "GPU"},
        {"4", "Filesystems"}, {"5", "Interfaces"},      {"6", "PROTO"},
        {"7", "COMMAND"},     {"8", "Sensors"},
    };

    for (const auto& c : cases) {
        for (int columns : {60, 100, 150}) {
            const PtyRun run = run_in_pty({"--interval", "1"}, columns, 45, 2.5,
                                          {{1.0, c.key}});
            ASSERT_FALSE(run.output.empty())
                << "no output for view " << c.key << " at " << columns << " columns";

            int worst = -1;
            EXPECT_LE(widest_line(run.output, &worst), static_cast<size_t>(columns))
                << "view " << c.key << " overflows line " << worst
                << " at " << columns << " columns";

            EXPECT_NE(last_frame(run.output).find(c.marker), std::string::npos)
                << "view " << c.key << " was not the frame on screen (expected "
                << c.marker << ")";
        }
    }
}

TEST(TuiPtyTest, DensityLadderRendersWithinTheTerminalWidth) {
    // The detailed levels print columns the normal layout leaves out, which is
    // exactly where a width budget stops adding up.
    for (const char* keys : {"+", "++", "+++", "-", "--", "m"}) {
        for (int columns : {60, 110}) {
            const PtyRun run = run_in_pty({"--interval", "1"}, columns, 45, 2.5,
                                          {{1.0, keys}});
            ASSERT_FALSE(run.output.empty());
            int worst = -1;
            EXPECT_LE(widest_line(run.output, &worst), static_cast<size_t>(columns))
                << "density '" << keys << "' overflows line " << worst
                << " at " << columns << " columns";
        }
    }
}

TEST(TuiPtyTest, DetailedProcessViewRendersWithinTheTerminalWidth) {
    // The widest layout in the program: the process table at full detail with
    // every optional column present.
    for (int columns : {60, 90, 120, 200}) {
        const PtyRun run = run_in_pty({"--interval", "1"}, columns, 45, 3.0,
                                      {{1.0, "7"}, {1.5, "+++"}, {2.0, "a"}});
        ASSERT_FALSE(run.output.empty());
        int worst = -1;
        EXPECT_LE(widest_line(run.output, &worst), static_cast<size_t>(columns))
            << "detailed process view overflows line " << worst
            << " at " << columns << " columns";
    }
}

TEST(TuiPtyTest, ViewBarMarksTheActiveView) {
    const PtyRun run = run_in_pty({"--interval", "1"}, 120, 40, 2.5, {{1.0, "5"}});
    const std::string plain = last_frame(run.output);
    // The strip names every view it could fit, so the bar itself is the proof
    // that a focus view is on screen.
    EXPECT_NE(plain.find("0:overview"), std::string::npos) << "view bar missing";
    EXPECT_NE(plain.find("5:net"), std::string::npos);
}

TEST(TuiPtyTest, EscapeLeavesAFocusViewBeforeItQuits) {
    // Escape is "back": out of a focus view first, out of the program only
    // from the overview, so a mistyped view change is not a quit.
    const PtyRun run = run_in_pty({"--interval", "1"}, 110, 40, 3.0,
                                  {{1.0, "7"}, {1.8, "\033"}});
    const std::string plain = last_frame(run.output);
    EXPECT_NE(plain.find("Load Average"), std::string::npos)
        << "Escape did not return to the overview";
    EXPECT_GE(run.frames, 3);
}

TEST(TuiPtyTest, ProcessInspectorOpensOnTheSelectedProcess) {
    const PtyRun run = run_in_pty({"--interval", "1"}, 120, 45, 3.5,
                                  {{1.0, "7"}, {1.5, "\033[B\033[B"}, {2.2, "\r"}});
    const std::string plain = last_frame(run.output);
    EXPECT_NE(plain.find("Resources"), std::string::npos)
        << "the process inspector never opened";
    EXPECT_NE(plain.find("Open files"), std::string::npos)
        << "the inspector did not reach its descriptor section";
}

TEST(TuiPtyTest, StartViewFlagOpensThatViewImmediately) {
    const PtyRun run = run_in_pty({"--interval", "1", "--view", "memory"}, 110, 40, 2.0);
    const std::string plain = last_frame(run.output);
    EXPECT_NE(plain.find("Physical memory"), std::string::npos);
    EXPECT_NE(plain.find("Swap & paging"), std::string::npos);
}

#else

TEST(TuiPtyTest, SkippedOnThisPlatform) {
    GTEST_SKIP() << "pty-based tests require POSIX; the Windows console path is "
                    "covered by the CI smoke tests instead";
}

#endif // SYSMON_POSIX
