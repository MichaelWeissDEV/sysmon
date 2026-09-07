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
    // Key reads must not block or consume anything when raw mode is not active.
    EXPECT_EQ(terminal::read_key_event().key, terminal::Key::None);
    terminal::disable_raw_input();   // must be a no-op, not a crash
}

TEST(TerminalTest, KeyEventDefaultsToNothingPending) {
    const terminal::KeyEvent none;
    EXPECT_EQ(none.key, terminal::Key::None);
    EXPECT_FALSE(static_cast<bool>(none));
    EXPECT_FALSE(none.is('q'));
    EXPECT_FALSE(none.is_either('q', 'Q'));
}

TEST(TerminalTest, KeyEventComparesCharactersBothWays) {
    terminal::KeyEvent ev;
    ev.key = terminal::Key::Char;
    ev.ch  = 'Q';
    EXPECT_TRUE(static_cast<bool>(ev));
    EXPECT_TRUE(ev.is('Q'));
    EXPECT_FALSE(ev.is('q'));
    EXPECT_TRUE(ev.is_either('q', 'Q'));

    // A named key is never a character, however it is compared.
    terminal::KeyEvent up;
    up.key = terminal::Key::Up;
    EXPECT_FALSE(up.is('A'));          // 'A' is the final byte of ESC [ A
    EXPECT_FALSE(up.is_either('a', 'A'));
    EXPECT_TRUE(static_cast<bool>(up));
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
