#include "sysmon/terminal.hpp"
#include "sysmon/platform.hpp"

#include <atomic>
#include <csignal>

#if defined(SYSMON_WINDOWS)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <conio.h>
#  include <io.h>
#  include <cstdio>
#else
#  include <poll.h>
#  include <sys/ioctl.h>
#  include <termios.h>
#  include <unistd.h>
#endif

namespace terminal {
namespace {

std::atomic<bool> g_resized{false};
bool g_raw_active = false;

#if defined(SYSMON_WINDOWS)

DWORD g_saved_out_mode = 0;
DWORD g_saved_in_mode  = 0;
UINT  g_saved_out_cp   = 0;
UINT  g_saved_in_cp    = 0;
bool  g_console_configured = false;
Size  g_last_size{};

#else

struct termios g_saved_tio{};
bool g_signal_installed  = false;

void winch_handler(int) {
    g_resized.store(true, std::memory_order_relaxed);
}

#endif

} // namespace

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

bool init() {
#if defined(SYSMON_WINDOWS)
    // UTF-8 in and out, so the block-drawing and arrow glyphs survive.
    g_saved_out_cp = GetConsoleOutputCP();
    g_saved_in_cp  = GetConsoleCP();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE || out == nullptr) return false;

    if (!GetConsoleMode(out, &g_saved_out_mode)) {
        return false;   // redirected to a file or pipe: no escape processing
    }
    const DWORD desired = g_saved_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                                           | ENABLE_PROCESSED_OUTPUT;
    if (!SetConsoleMode(out, desired)) {
        return false;   // pre-Windows-10 console: ANSI is not available
    }
    g_console_configured = true;
    g_last_size = size();
    return true;
#else
    if (!g_signal_installed) {
#  ifdef SIGWINCH
        std::signal(SIGWINCH, winch_handler);
#  endif
        g_signal_installed = true;
    }
    return stdout_is_tty();
#endif
}

void shutdown() {
    disable_raw_input();
#if defined(SYSMON_WINDOWS)
    if (g_console_configured) {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out != INVALID_HANDLE_VALUE && out != nullptr) {
            SetConsoleMode(out, g_saved_out_mode);
        }
        g_console_configured = false;
    }
    if (g_saved_out_cp != 0) SetConsoleOutputCP(g_saved_out_cp);
    if (g_saved_in_cp  != 0) SetConsoleCP(g_saved_in_cp);
#endif
}

// ---------------------------------------------------------------------------
// TTY detection
// ---------------------------------------------------------------------------

bool stdout_is_tty() {
#if defined(SYSMON_WINDOWS)
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

bool stdin_is_tty() {
#if defined(SYSMON_WINDOWS)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

// ---------------------------------------------------------------------------
// Raw keyboard input
// ---------------------------------------------------------------------------

bool enable_raw_input() {
    if (g_raw_active) return true;
    if (!stdin_is_tty()) return false;

#if defined(SYSMON_WINDOWS)
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    if (in == INVALID_HANDLE_VALUE || in == nullptr) return false;
    if (!GetConsoleMode(in, &g_saved_in_mode)) return false;

    // Drop line buffering and echo; _kbhit()/_getch() then see every keypress.
    DWORD mode = g_saved_in_mode;
    mode &= ~static_cast<DWORD>(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    mode |= ENABLE_WINDOW_INPUT;
    if (!SetConsoleMode(in, mode)) return false;
#else
    if (tcgetattr(STDIN_FILENO, &g_saved_tio) != 0) return false;

    // VMIN=0/VTIME=0 makes read() return immediately when no key is pending,
    // which is all the non-blocking behaviour that is needed here.
    //
    // Deliberately NOT setting O_NONBLOCK on stdin: in a terminal, stdin and
    // stdout are the same open file description, so that flag makes *stdout*
    // non-blocking too.  Once the terminal's output buffer filled, std::cout's
    // write would fail with EAGAIN, the stream would latch its error state, and
    // every later frame would be discarded — the dashboard would go blank and
    // never come back.
    struct termios raw = g_saved_tio;
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return false;
#endif

    g_raw_active = true;
    return true;
}

void disable_raw_input() {
    if (!g_raw_active) return;

#if defined(SYSMON_WINDOWS)
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    if (in != INVALID_HANDLE_VALUE && in != nullptr) {
        SetConsoleMode(in, g_saved_in_mode);
    }
#else
    tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_tio);
#endif

    g_raw_active = false;
}

// ---------------------------------------------------------------------------
// Decoded key input
// ---------------------------------------------------------------------------

namespace {

/// Milliseconds to wait after an Escape byte for a sequence introducer.
///
/// A terminal emits the whole of "ESC [ A" in one burst, so a real arrow key
/// always has its introducer waiting well inside this window.  A user pressing
/// Escape pays the delay once, which is imperceptible against a dashboard that
/// redraws on a multi-second interval.
constexpr int kEscapeSequenceTimeoutMs = 40;

KeyEvent as_char(char c) {
    KeyEvent e;
    e.key = Key::Char;
    e.ch  = c;
    return e;
}

KeyEvent as_key(Key k) {
    KeyEvent e;
    e.key = k;
    return e;
}

#if !defined(SYSMON_WINDOWS)

/// Read one byte, waiting at most `timeout_ms`.  Returns -1 on timeout.
int read_byte_within(int timeout_ms) {
    struct pollfd pfd{};
    pfd.fd     = STDIN_FILENO;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, timeout_ms) <= 0)   return -1;
    if ((pfd.revents & POLLIN) == 0)      return -1;

    char ch = 0;
    if (::read(STDIN_FILENO, &ch, 1) <= 0) return -1;
    return static_cast<unsigned char>(ch);
}

/// Map the final byte of a CSI/SS3 sequence to a key.
Key key_from_final(int final_byte) {
    switch (final_byte) {
        case 'A': return Key::Up;
        case 'B': return Key::Down;
        case 'C': return Key::Right;
        case 'D': return Key::Left;
        case 'H': return Key::Home;
        case 'F': return Key::End;
        case 'P': return Key::F1;   // SS3 form
        case 'Q': return Key::F2;
        case 'R': return Key::F3;
        case 'S': return Key::F4;
        default:  return Key::None;
    }
}

/// Map the numeric parameter of a "CSI <n> ~" sequence to a key.
Key key_from_tilde(int n) {
    switch (n) {
        case 1:  case 7:  return Key::Home;
        case 2:            return Key::Insert;
        case 3:            return Key::Delete;
        case 4:  case 8:  return Key::End;
        case 5:            return Key::PageUp;
        case 6:            return Key::PageDown;
        case 11: return Key::F1;   case 12: return Key::F2;
        case 13: return Key::F3;   case 14: return Key::F4;
        case 15: return Key::F5;   case 17: return Key::F6;
        case 18: return Key::F7;   case 19: return Key::F8;
        case 20: return Key::F9;   case 21: return Key::F10;
        case 23: return Key::F11;  case 24: return Key::F12;
        default: return Key::None;
    }
}

/// Consume the remainder of an escape sequence whose introducer was read.
KeyEvent decode_escape_sequence(int introducer) {
    if (introducer == 'O') {
        // SS3: ESC O <final>.  Used by some terminals for arrows and F1-F4.
        const int final_byte = read_byte_within(kEscapeSequenceTimeoutMs);
        if (final_byte < 0) return as_key(Key::Escape);
        const Key k = key_from_final(final_byte);
        return k == Key::None ? as_key(Key::None) : as_key(k);
    }

    if (introducer != '[') {
        // Alt+<key> arrives as ESC <key>.  Nothing here binds Alt, so report
        // the Escape and let the character be read on the next call.
        return as_key(Key::Escape);
    }

    // CSI: ESC [ <params> <final>.  Parameters are digits and semicolons.
    int  param = 0;
    bool have_param = false;
    for (int guard = 0; guard < 16; ++guard) {
        const int b = read_byte_within(kEscapeSequenceTimeoutMs);
        if (b < 0) return as_key(Key::Escape);

        if (b >= '0' && b <= '9') {
            param = param * 10 + (b - '0');
            have_param = true;
            continue;
        }
        if (b == ';') {
            // Modifier parameters (e.g. "ESC [ 1 ; 5 A" for Ctrl+Up).  The
            // modifier is not needed, so restart parameter accumulation and
            // keep scanning for the final byte.
            param = 0;
            have_param = false;
            continue;
        }
        if (b == '~') {
            const Key k = have_param ? key_from_tilde(param) : Key::None;
            return k == Key::None ? as_key(Key::None) : as_key(k);
        }
        const Key k = key_from_final(b);
        return k == Key::None ? as_key(Key::None) : as_key(k);
    }
    return as_key(Key::None);
}

#endif // !SYSMON_WINDOWS

} // namespace

KeyEvent read_key_event() {
    if (!g_raw_active) return KeyEvent{};

#if defined(SYSMON_WINDOWS)
    if (_kbhit() == 0) return KeyEvent{};
    int ch = _getch();

    // Extended keys arrive as a two-byte sequence introduced by 0x00 or 0xE0.
    if (ch == 0 || ch == 0xE0) {
        if (_kbhit() == 0) return KeyEvent{};
        const int code = _getch();
        switch (code) {
            case 72: return as_key(Key::Up);
            case 80: return as_key(Key::Down);
            case 75: return as_key(Key::Left);
            case 77: return as_key(Key::Right);
            case 73: return as_key(Key::PageUp);
            case 81: return as_key(Key::PageDown);
            case 71: return as_key(Key::Home);
            case 79: return as_key(Key::End);
            case 83: return as_key(Key::Delete);
            case 82: return as_key(Key::Insert);
            case 59: return as_key(Key::F1);   case 60: return as_key(Key::F2);
            case 61: return as_key(Key::F3);   case 62: return as_key(Key::F4);
            case 63: return as_key(Key::F5);   case 64: return as_key(Key::F6);
            case 65: return as_key(Key::F7);   case 66: return as_key(Key::F8);
            case 67: return as_key(Key::F9);   case 68: return as_key(Key::F10);
            case 133: return as_key(Key::F11); case 134: return as_key(Key::F12);
            default: return KeyEvent{};
        }
    }

    switch (ch) {
        case '\r': case '\n': return as_key(Key::Enter);
        case 27:              return as_key(Key::Escape);
        case '\t':            return as_key(Key::Tab);
        case 8: case 127:     return as_key(Key::Backspace);
        default:              return as_char(static_cast<char>(ch));
    }
#else
    const int first = read_byte_within(0);
    if (first < 0) return KeyEvent{};

    switch (first) {
        case '\r': case '\n': return as_key(Key::Enter);
        case '\t':            return as_key(Key::Tab);
        case 8: case 127:     return as_key(Key::Backspace);
        default: break;
    }

    if (first != 27) return as_char(static_cast<char>(first));

    // Escape: either the key itself or the start of a sequence.  Only a timed
    // read can tell them apart, and the terminal sends the rest of a real
    // sequence immediately, so a short wait resolves it without a visible
    // delay on the Escape key.
    const int introducer = read_byte_within(kEscapeSequenceTimeoutMs);
    if (introducer < 0) return as_key(Key::Escape);
    return decode_escape_sequence(introducer);
#endif
}

Size size() {
    Size s;
#if defined(SYSMON_WINDOWS)
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != INVALID_HANDLE_VALUE && out != nullptr &&
        GetConsoleScreenBufferInfo(out, &info)) {
        const int w = info.srWindow.Right  - info.srWindow.Left + 1;
        const int h = info.srWindow.Bottom - info.srWindow.Top  + 1;
        if (w > 0) s.width  = w;
        if (h > 0) s.height = h;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col > 0) s.width  = ws.ws_col;
        if (ws.ws_row > 0) s.height = ws.ws_row;
    }
#endif
    return s;
}

bool resized() {
#if defined(SYSMON_WINDOWS)
    // Windows consoles have no SIGWINCH equivalent, so compare dimensions.
    const Size current = size();
    if (current.width != g_last_size.width || current.height != g_last_size.height) {
        g_last_size = current;
        return true;
    }
    return false;
#else
    return g_resized.exchange(false, std::memory_order_relaxed);
#endif
}

} // namespace terminal
