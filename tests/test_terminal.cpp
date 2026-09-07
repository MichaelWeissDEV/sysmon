#include <gtest/gtest.h>

#include "sysmon/terminal.hpp"

// The terminal layer is the piece that lets the same main loop drive a POSIX
// termios terminal and a Windows console.  It has to stay safe when there is no
// terminal at all, which is exactly the situation under a test runner or CI.

TEST(TerminalTest, InitAndShutdownAreSafeWithoutATerminal) {
    terminal::init();
    terminal::shutdown();
    terminal::init();
    terminal::shutdown();
    SUCCEED();
}

TEST(TerminalTest, RawInputIsNotEnteredWhenStdinIsNotATty) {
    if (terminal::stdin_is_tty()) {
        GTEST_SKIP() << "stdin is a terminal; this check needs a redirected stdin";
    }
    EXPECT_FALSE(terminal::enable_raw_input());
    // read_key() must not block or read anything when raw mode is not active.
    EXPECT_EQ(terminal::read_key(), -1);
    terminal::disable_raw_input();   // must be a no-op, not a crash
}

TEST(TerminalTest, SizeAlwaysReturnsUsableDimensions) {
    const terminal::Size size = terminal::size();
    EXPECT_GT(size.width, 0);
    EXPECT_GT(size.height, 0);
}

TEST(TerminalTest, ResizedIsQueryableAndConsumesTheFlag) {
    terminal::init();
    (void)terminal::resized();
    // With no resize in between, a second query must report no change.
    EXPECT_FALSE(terminal::resized());
    terminal::shutdown();
}
