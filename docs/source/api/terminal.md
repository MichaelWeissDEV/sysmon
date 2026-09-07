# terminal

Cross-platform terminal control. Everything in sysmon that touches the
controlling terminal goes through this layer, so the rest of the program
contains no `termios.h` and no `windows.h`.

```cpp
#include "sysmon/terminal.hpp"
```

On Windows this is also where ANSI escape processing and UTF-8 console output
are switched on. Without `ENABLE_VIRTUAL_TERMINAL_PROCESSING` every escape
sequence the dashboard emits would print as literal text, and without
`SetConsoleOutputCP(CP_UTF8)` the block-drawing and arrow glyphs would mojibake.

| Function | Purpose |
|----------|---------|
| `init()` / `shutdown()` | Configure and restore the terminal |
| `stdout_is_tty()` / `stdin_is_tty()` | Decide between dashboard and plain output |
| `enable_raw_input()` / `disable_raw_input()` | Unbuffered, non-echoing key reads |
| `read_key()` | One pending keystroke, or `-1` when none is available |
| `size()` | Current dimensions, defaulting to 80x24 |
| `resized()` | SIGWINCH on POSIX; a size comparison on Windows, which has no equivalent signal |

> Full Doxygen API documentation is generated automatically when building with `cmake -DCMAKE_BUILD_TYPE=Release` and Doxygen installed.
