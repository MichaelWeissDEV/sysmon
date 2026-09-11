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

    // The run ends with SIGKILL, which can land in the middle of a write, so
    // the final piece may be a partial frame.  Prefer the one before it, which
    // is bounded by the next frame's cursor-home and therefore complete.
    const size_t previous = output.rfind("\033[H", last - 1);
    if (previous != std::string::npos && last > 0) {
        return strip_escapes(output.substr(previous, last - previous));
    }
    return strip_escapes(output.substr(last));
}

/// Every complete frame in a capture, escape-stripped.
///
/// Checking each frame rather than only the last is both stronger and cheaper:
/// one pty run can visit every view in turn, and a layout that overflowed only
/// while passing through no longer escapes notice.
std::vector<std::string> all_frames(const std::string& output) {
    std::vector<std::string> result;
    size_t pos = output.find("\033[H");
    while (pos != std::string::npos) {
        const size_t next = output.find("\033[H", pos + 1);
        if (next == std::string::npos) break;   // may be a partial final frame
        result.push_back(strip_escapes(output.substr(pos, next - pos)));
        pos = next;
    }
    return result;
}

/// Display width of one line: a UTF-8 lead byte is one column, a continuation
/// byte is none, which is what the box-drawing and arrow glyphs need.
size_t line_width(const std::string& line) {
    size_t width = 0;
    for (char c : line) {
        if ((static_cast<unsigned char>(c) & 0xC0) == 0x80) continue;
        ++width;
    }
    return width;
}

/// Widest line and tallest frame across every complete frame of a capture.
void measure_all(const std::string& output, size_t* widest, int* tallest,
                 std::string* worst_line) {
    *widest  = 0;
    *tallest = 0;
    if (worst_line != nullptr) worst_line->clear();

    for (const std::string& frame : all_frames(output)) {
        int rows = 0;
        size_t start = 0;
        while (start <= frame.size()) {
            const size_t end  = frame.find('\n', start);
            const std::string line = frame.substr(start, end == std::string::npos
                                                          ? std::string::npos
                                                          : end - start);
            const size_t width = line_width(line);
            if (width > *widest) {
                *widest = width;
                if (worst_line != nullptr) *worst_line = line;
            }
            if (end == std::string::npos) break;
            ++rows;
            start = end + 1;
        }
        if (rows > *tallest) *tallest = rows;
    }
}

/// Painted rows of the last complete frame.
///
/// A frame taller than the terminal makes the terminal *scroll*, which moves
/// every later frame's cursor-home to the wrong row and tears the display
/// apart — invisible to a width-only check.
int frame_height(const std::string& output) {
    const std::string frame = last_frame(output);
    if (frame.empty()) return 0;
    int rows = static_cast<int>(std::count(frame.begin(), frame.end(), '\n'));
    // The final newline ends the last painted row rather than starting a new
    // one, so it is already counted correctly by counting newlines.
    return rows;
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
    // Each view is its own layout and can overflow on its own, but they do not
    // need a process each: one run visits all eight, and every frame it
    // painted along the way is checked — which catches a view that overflowed
    // only while being passed through.
    static const char* markers[] = {
        "Processor", "Physical memory", "GPU", "Filesystems",
        "Interfaces", "PROTO", "COMMAND", "Sensors",
    };

    for (int columns : {60, 100, 150}) {
        const PtyRun run = run_in_pty(
            {"--interval", "1"}, columns, 45, 7.0,
            {{1.0, "1"}, {1.6, "2"}, {2.2, "3"}, {2.8, "4"},
             {3.4, "5"}, {4.0, "6"}, {4.6, "7"}, {5.2, "8"}});
        ASSERT_FALSE(run.output.empty()) << "no output at " << columns << " columns";

        size_t widest = 0;
        int tallest = 0;
        std::string worst;
        measure_all(run.output, &widest, &tallest, &worst);
        EXPECT_LE(widest, static_cast<size_t>(columns))
            << "a frame overflowed a " << columns << "-column terminal:\n|" << worst << "|";
        EXPECT_LE(tallest, 45) << "a frame was taller than the terminal";

        // Each view has to have actually been on screen at some point.
        const std::vector<std::string> frames = all_frames(run.output);
        for (const char* marker : markers) {
            const bool seen = std::any_of(frames.begin(), frames.end(),
                                          [&](const std::string& f) {
                                              return f.find(marker) != std::string::npos;
                                          });
            EXPECT_TRUE(seen) << "no frame ever showed " << marker
                              << " at " << columns << " columns";
        }
    }
}

TEST(TuiPtyTest, DensityLadderRendersWithinTheTerminalWidth) {
    // The detailed levels print columns the normal layout leaves out, which is
    // exactly where a width budget stops adding up.  One run walks the whole
    // ladder up and back down; every frame on the way is checked.
    for (int columns : {60, 110}) {
        const PtyRun run = run_in_pty({"--interval", "1"}, columns, 45, 5.0,
                                      {{1.0, "+"}, {1.6, "+"}, {2.2, "+"},
                                       {2.8, "-"}, {3.4, "-"}, {4.0, "m"}});
        ASSERT_FALSE(run.output.empty());

        size_t widest = 0;
        int tallest = 0;
        std::string worst;
        measure_all(run.output, &widest, &tallest, &worst);
        EXPECT_LE(widest, static_cast<size_t>(columns))
            << "a density level overflowed " << columns << " columns:\n|" << worst << "|";
        EXPECT_LE(tallest, 45);
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
    const PtyRun run = run_in_pty({"--interval", "1"}, 120, 40, 3.5, {{1.0, "5"}});
    const std::string plain = last_frame(run.output);
    // The strip names every view it could fit, so the bar itself is the proof
    // that a focus view is on screen.
    EXPECT_NE(plain.find("0:overview"), std::string::npos) << "view bar missing";
    EXPECT_NE(plain.find("5:net"), std::string::npos);
}

TEST(TuiPtyTest, EscapeLeavesAFocusViewBeforeItQuits) {
    // Escape is "back": out of a focus view first, out of the program only
    // from the overview, so a mistyped view change is not a quit.
    // Runs well past the last keystroke: last_frame() deliberately takes the
    // frame *before* the final one, since a SIGKILL can cut the final write
    // short, so the state under test needs a whole frame painted after it.
    const PtyRun run = run_in_pty({"--interval", "1"}, 110, 40, 4.5,
                                  {{1.0, "7"}, {1.8, "\033"}});
    const std::string plain = last_frame(run.output);
    EXPECT_NE(plain.find("Load Average"), std::string::npos)
        << "Escape did not return to the overview";
    EXPECT_GE(run.frames, 3);
}

TEST(TuiPtyTest, ProcessInspectorOpensOnTheSelectedProcess) {
    const PtyRun run = run_in_pty({"--interval", "1"}, 120, 45, 5.0,
                                  {{1.0, "7"}, {1.5, "\033[B\033[B"}, {2.2, "\r"}});
    const std::string plain = last_frame(run.output);
    EXPECT_NE(plain.find("Resources"), std::string::npos)
        << "the process inspector never opened";
    EXPECT_NE(plain.find("Open files"), std::string::npos)
        << "the inspector did not reach its descriptor section";
}

TEST(TuiPtyTest, StartViewFlagOpensThatViewImmediately) {
    const PtyRun run = run_in_pty({"--interval", "1", "--view", "memory"}, 110, 40, 3.0);
    const std::string plain = last_frame(run.output);
    EXPECT_NE(plain.find("Physical memory"), std::string::npos);
    EXPECT_NE(plain.find("Swap & paging"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Extreme geometry
// ---------------------------------------------------------------------------

TEST(TuiPtyTest, FramesNeverExceedTheTerminalHeight) {
    // Vertical overflow is as destructive as horizontal and was invisible to
    // every check here until now: the fixed content of a view is printed
    // whatever the height, so a 15-row terminal used to receive a 46-row frame.
    struct Geometry { int cols; int rows; };
    static constexpr Geometry geometries[] = {{40, 15}, {80, 24}, {120, 40}};

    for (const auto& g : geometries) {
        const PtyRun run = run_in_pty({"--interval", "1"}, g.cols, g.rows, 5.0,
                                      {{1.0, "+++"}, {1.6, "2"}, {2.4, "4"},
                                       {3.2, "7"}, {4.0, "0"}});
        ASSERT_FALSE(run.output.empty())
            << "no output at " << g.cols << "x" << g.rows;

        size_t widest = 0;
        int tallest = 0;
        std::string worst;
        measure_all(run.output, &widest, &tallest, &worst);

        EXPECT_LE(tallest, g.rows)
            << "a frame was taller than a " << g.cols << "x" << g.rows << " terminal";
        EXPECT_LE(widest, static_cast<size_t>(g.cols))
            << "a frame overflowed " << g.cols << " columns:\n|" << worst << "|";
    }
}

TEST(TuiPtyTest, VeryNarrowTerminalsStillRenderSomething) {
    // Below 55 columns five separate tables used to overflow, each because a
    // fixed cost plus a flexible column's own minimum added up past the
    // terminal.  20x5 is the floor worth supporting: a split pane.
    for (int columns : {20, 30}) {
        const PtyRun run = run_in_pty({"--interval", "1"}, columns, 8, 2.5, {{1.0, "7"}});
        ASSERT_FALSE(run.output.empty()) << "no output at " << columns << " columns";

        int worst = -1;
        EXPECT_LE(widest_line(run.output, &worst), static_cast<size_t>(columns))
            << "line " << worst << " overflows a " << columns << "-column terminal";
        EXPECT_LE(frame_height(run.output), 8);

        // Still useful, not merely non-overflowing: a PID has to survive.
        EXPECT_NE(last_frame(run.output).find("PID"), std::string::npos)
            << "nothing recognisable rendered at " << columns << " columns";
    }
}

// ---------------------------------------------------------------------------
// Adversarial key input
// ---------------------------------------------------------------------------

TEST(TuiPtyTest, PartialEscapeSequenceDoesNotQuit) {
    // A terminal can emit an escape sequence that the decoder never sees the
    // end of.  Both timeout branches fall back to Key::Escape, and Escape in
    // the overview quits — so a truncated sequence would kill the dashboard.
    const PtyRun run = run_in_pty({"--interval", "1"}, 100, 30, 4.0,
                                  {{1.0, "7"},          // leave the overview first
                                   {1.5, "\033["},      // CSI with no final byte
                                   {2.2, "\033[1;"},    // parameters, then nothing
                                   {2.9, "\033O"}});    // SS3 with no final byte

    EXPECT_GE(run.frames, 3) << "the dashboard stopped painting after a partial sequence";
    EXPECT_GT(run.output.size(), 15000u);
}

TEST(TuiPtyTest, RapidKeyInputIsSurvivable) {
    // Everything at once: view changes, density changes, paging and a
    // selection, typed faster than the refresh interval.
    const PtyRun run = run_in_pty({"--interval", "1"}, 110, 35, 4.0,
                                  {{1.0, "12345678707"},
                                   {1.4, "+++---+"},
                                   {1.8, "\033[B\033[B\033[B\033[A\033[6~\033[5~"},
                                   {2.2, "\033[F\033[H"},
                                   {2.6, "aa"},
                                   {3.0, "\r"}});

    EXPECT_GE(run.frames, 3) << "the dashboard stopped painting under rapid input";
    int worst = -1;
    EXPECT_LE(widest_line(run.output, &worst), 110u)
        << "line " << worst << " overflowed after rapid input";
}

TEST(TuiPtyTest, UnboundKeysAreIgnored) {
    // An unbound key must not repaint, quit, or corrupt the view.
    const PtyRun run = run_in_pty({"--interval", "1"}, 100, 30, 4.0,
                                  {{1.0, "5"}, {1.6, "xyzXYZ!@#$%^&*()"}});
    EXPECT_GE(run.frames, 2);
    EXPECT_NE(last_frame(run.output).find("Interfaces"), std::string::npos)
        << "unbound keys changed the view";
}

#else

TEST(TuiPtyTest, SkippedOnThisPlatform) {
    GTEST_SKIP() << "pty-based tests require POSIX; the Windows console path is "
                    "covered by the CI smoke tests instead";
}

#endif // SYSMON_POSIX
