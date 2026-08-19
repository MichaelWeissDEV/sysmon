# sysmon

A comprehensive, cross-platform terminal-based system monitor written in modern C++20.

[![Read the Docs](https://readthedocs.org/projects/sysmon/badge/?version=latest)](https://sysmon.readthedocs.io)

## Features

| Feature | Description |
|---------|-------------|
| CPU | Model, per-core usage + sparklines, frequency, temperature |
| Memory | RAM, cache, buffers, swap with progress bars |
| Temperatures | All hwmon/thermal sensors with labels and thresholds |
| Disk | Usage per filesystem + read/write I/O throughput |
| Network | Per-interface rx/tx throughput, IP address, link speed |
| Processes | Top processes by CPU: user, RSS, VMS, threads, state |
| Load | 1/5/15-minute load averages + process counts |
| Live TUI | Full-screen ANSI dashboard with sparkline history |
| One-shot | Plain text output for scripts and logging |

## Supported Platforms

| Platform | Status |
|----------|--------|
| Linux    | Full support (`/proc`, `/sys`, hwmon) |
| macOS    | Supported (Intel + Apple Silicon, Mach APIs; some metrics are N/A — see below) |
| Windows  | Planned |

### macOS metric availability

macOS does not expose every metric via an unprivileged, stable API. The following
are reported as `N/A` rather than guessed:

| Metric | macOS |
|--------|-------|
| CPU usage, per-core usage | ✓ |
| CPU frequency | N/A on Apple Silicon (no `hw.cpufrequency`); Intel only when the sysctl exists |
| CPU package temperature | N/A (thermal pressure is not a temperature) |
| GPU usage / frequency / memory usage | N/A |
| GPU cores | Only when `hw.gpu.count` exists |
| Unified memory capacity | ✓ (from `hw.memsize`) |
| Network throughput, addresses | ✓ |
| Network link speed | N/A |
| Battery temperature | ✓ (via IOKit, when present) |
| Disk capacity + I/O | ✓ |
| Active TCP/UDP connections with PID | ✓ (parsed from `netstat -anv`) |

## Quick Start

```bash
# Build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Live dashboard
./build/sysmon

# One-shot output (great for scripts)
./build/sysmon --once

# Custom refresh interval (seconds)
./build/sysmon --interval 1
```

## Example Output (--once)

```
System
  Hostname      archbox
  OS            Arch Linux
  Kernel        6.9.1-arch1
  Architecture  x86_64
  Uptime        3h 42m 15s

CPU
  Model         AMD Ryzen 9 5950X 16-Core Processor
  Cores         32 logical / 16 physical
  Usage         18.4 %
  usr/sys       14.2 % / 3.8 %
  Frequency     4200 MHz / 4900 MHz max
  Temperature   61.0 °C

Memory
  RAM           18.4 GB / 64.0 GB  (28.7 %)
  Cached        12.0 GB
  Swap           0.0 GB / 16.0 GB   (0.0 %)

Load
  1 min         1.21
  5 min         1.04
  15 min        0.97
  Processes     3 / 312

Temperatures
  coretemp: Package id 0   62.0 °C
  coretemp: Core 0         59.0 °C
  coretemp: Core 1         61.0 °C

Disks
  /                   200.0 GB / 1.0 TB   20.0 %  [ext4]
  /home               500.0 GB / 2.0 TB   25.0 %  [ext4]

  Disk I/O
  sda          read:      0.0 B/s  write:    4.2 MB/s

Network
  eth0        192.168.1.100   ↓    1.2 MB/s  ↑   240.0 KB/s

Processes (top 15 by CPU)
     PID  NAME            USER         CPU%    MEM%       RSS
    1234  firefox         alice        12.3     3.1  512.0 MB
    5678  code            alice         4.7     1.2  192.0 MB
```

## Keyboard Shortcuts (TUI Mode)

| Key | Action |
|-----|--------|
| `q` / `Q` | Quit |
| `r` / `R` | Force immediate refresh |
| `Ctrl+C` | Quit |

## Building

### Dependencies

- C++20 compiler (GCC 10+, Clang 12+, AppleClang)
- CMake 3.16+
- No external runtime libraries

### Linux

```bash
# Debian/Ubuntu
sudo apt install cmake g++

# Arch Linux
sudo pacman -S cmake gcc
```

### macOS

```bash
xcode-select --install
brew install cmake
```

### Compile

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
sudo cmake --install build      # install to /usr/local/bin
```

### Single-file (Linux)

```bash
g++ -std=c++20 -O2 -I include src/*.cpp -o sysmon
```

### Single-file (macOS)

```bash
clang++ -std=c++20 -O2 -I include src/*.cpp \
    -framework IOKit -framework CoreFoundation -o sysmon
```

## Tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

## Documentation

Full documentation is available at **[sysmon.readthedocs.io](https://sysmon.readthedocs.io)** (once connected to Read the Docs).

To build locally:

```bash
cd docs
pip install -r requirements.txt
sphinx-build source _build/html
open _build/html/index.html
```

## Architecture

```
main.cpp  →  [Monitors]  →  [Stats structs]  →  TUI / TextRenderer
                 │
    ┌────────────┼──────────────────────┐
    │            │                      │
 CpuMonitor  MemoryMonitor  NetworkMonitor
 DiskMonitor DiskIOMonitor  ProcessMonitor
 TempMonitor  LoadMonitor   SystemMonitor
```

See [Architecture documentation](docs/source/architecture.md) for full details.

## License

MIT
