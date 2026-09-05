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
 * @brief Read one pending keystroke without blocking.
 *
 * @return the character, or -1 when no key is available.
 */
int read_key();

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
