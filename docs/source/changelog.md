# Changelog

## v0.2.0 (2025)

### New Features

- **Live TUI Dashboard** — Full-screen ANSI color dashboard with:
  - Sparkline history graphs for CPU, memory, and network
  - Progress bars with color coding (green/yellow/red by threshold)
  - Per-core CPU breakdown
  - Keyboard shortcuts (q to quit, r to refresh)
- **Network Monitoring** — Per-interface rx/tx throughput, IP address, link speed
- **Disk I/O Monitoring** — Per-device read/write throughput from `/proc/diskstats`
- **Process Monitor** — Top processes by CPU with user, RSS, VMS, thread count, state
- **Extended Temperature Sensors** — All hwmon sensors with labels and thresholds
- **macOS Support** — Full port using sysctl, Mach APIs, and getifaddrs
- **Per-core CPU stats** — Individual core usage and frequency
- **Detailed CPU time breakdown** — user%, sys%, iowait%, idle%
- **Extended system info** — Architecture, raw uptime seconds

### Improvements

- `--once` flag for script-friendly one-shot output
- `--no-tui` for plain text live refresh
- `--interval N` to control refresh period
- Keyboard input handling (non-blocking)
- Rate monitors (network, disk I/O) carry internal state for accurate delta computation
- Extended virtual filesystem filter in disk monitor
- Stable sort: root filesystem always first

### Bug Fixes

- Correct CPU usage calculation using per-core `/proc/stat` lines
- Fixed duplicate `enable_testing()` in CMakeLists.txt
- Fixed macOS uptime via `kern.boottime` sysctl

---

## v0.1.0 (initial)

- CPU, memory, load, disk, temperature (Linux only)
- Plain text output
- CMake build
