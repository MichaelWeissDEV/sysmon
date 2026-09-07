/**
 * @file terminal.hpp
 * @brief Cross-platform terminal control (POSIX termios / Windows console).
 *
 * Everything in sysmon that touches the controlling terminal goes through this
 * layer, so the rest of the program can stay free of `termios.h` and
 * `windows.h`.  On Windows this is also where ANSI escape processing and UTF-8
 * output get switched on — without them the dashboard's escape sequences and
 * box-drawing characters would print as literal garbage.
 */

#ifndef SYSMON_TERMINAL_HPP
#define SYSMON_TERMINAL_HPP

namespace terminal {

/** @brief Terminal dimensions in character cells. */
struct Size {
    int width{80};
    int height{24};
};

/**
 * @brief A decoded keystroke.
 *
 * Arrow, navigation and function keys arrive as multi-byte sequences that mean
 * nothing to a caller comparing single characters, and on POSIX every one of
 * them starts with the same byte as the Escape key.  Decoding happens here so
 * no caller has to know that.
 */
enum class Key {
    None = 0,   ///< Nothing was pending
    Char,       ///< A printable character; see KeyEvent::ch
    Enter,
    Escape,
    Tab,
    Backspace,
    Up,
    Down,
    Left,
    Right,
    PageUp,
    PageDown,
    Home,
    End,
    Delete,
    Insert,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
};

/** @brief One keystroke: a Key, plus the character when Key::Char. */
struct KeyEvent {
    Key  key{Key::None};
    char ch{0};

    /** @brief True when this is the given printable character. */
    bool is(char c) const { return key == Key::Char && ch == c; }

    /** @brief True when this is either case of the given letter. */
    bool is_either(char lower, char upper) const {
        return key == Key::Char && (ch == lower || ch == upper);
    }

    explicit operator bool() const { return key != Key::None; }
};

/**
 * @brief Prepare the terminal for output.
 *
 * On Windows: switches the console to UTF-8 and enables virtual-terminal
 * processing.  On POSIX: installs the SIGWINCH handler used by resized().
 * Safe to call when stdout is redirected — it simply does nothing useful then.
 *
 * @return true when ANSI escape sequences are expected to render correctly.
 */
bool init();

/** @brief Undo everything init() and enable_raw_input() changed. */
void shutdown();

/** @brief True when standard output is attached to an interactive terminal. */
bool stdout_is_tty();

/** @brief True when standard input is attached to an interactive terminal. */
bool stdin_is_tty();

/**
 * @brief Switch stdin to unbuffered, non-echoing, non-blocking key reads.
 *
 * @return true if raw mode was entered (false when stdin is not a terminal).
 */
bool enable_raw_input();

/** @brief Restore the input mode saved by enable_raw_input(). */
void disable_raw_input();

/**
 * @brief Read one pending keystroke, decoding escape sequences.
 *
 * Never blocks waiting for a *first* byte.  It does wait briefly (a few tens of
 * milliseconds) after an Escape to see whether a sequence introducer follows,
 * because that is the only way to tell the Escape key from the first byte of
 * an arrow key; a terminal sends the rest of the sequence in the same burst, so
 * the wait is bounded and only pays out on an actual Escape press.
 *
 * @return a KeyEvent whose key is Key::None when nothing was pending.
 */
KeyEvent read_key_event();

/** @brief Current terminal size, falling back to 80x24 when unknown. */
Size size();

/**
 * @brief Whether the terminal changed size since the last call.
 *
 * Uses SIGWINCH on POSIX and a size comparison on Windows, which has no
 * equivalent signal.  Consumes the flag.
 */
bool resized();

} // namespace terminal

#endif // SYSMON_TERMINAL_HPP
