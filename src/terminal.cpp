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
#  include <fcntl.h>
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
int  g_saved_stdin_flags = 0;
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

    struct termios raw = g_saved_tio;
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return false;

    g_saved_stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, g_saved_stdin_flags | O_NONBLOCK);
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
    fcntl(STDIN_FILENO, F_SETFL, g_saved_stdin_flags);
#endif

    g_raw_active = false;
}

int read_key() {
    if (!g_raw_active) return -1;

#if defined(SYSMON_WINDOWS)
    if (_kbhit() == 0) return -1;
    int ch = _getch();
    // Function and arrow keys arrive as a two-byte sequence; swallow the second
    // byte so it is not mistaken for a command character.
    if (ch == 0 || ch == 0xE0) {
        if (_kbhit() != 0) _getch();
        return -1;
    }
    return ch;
#else
    char ch = 0;
    const ssize_t n = ::read(STDIN_FILENO, &ch, 1);
    if (n <= 0) return -1;
    return static_cast<unsigned char>(ch);
#endif
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

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
