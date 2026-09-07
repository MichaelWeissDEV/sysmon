# sysmon

A comprehensive, cross-platform terminal system monitor written in modern C++20.
No runtime dependencies — one static binary that reads what the operating system
actually exposes, and says `N/A` for everything it cannot measure.

[![CI](https://github.com/MichaelWeissDEV/sysmon/actions/workflows/ci.yml/badge.svg)](https://github.com/MichaelWeissDEV/sysmon/actions/workflows/ci.yml)
[![Read the Docs](https://readthedocs.org/projects/sysmon/badge/?version=latest)](https://sysmon.readthedocs.io)

## Supported Platforms

| Platform | Status | Verified by |
|----------|--------|-------------|
| Linux    | Full support (`/proc`, `/sys`, hwmon, netlink) | CI: build, tests, run |
| macOS    | Full support (Intel + Apple Silicon, Mach/IOKit) | CI: build, tests, run |
| Windows  | Full support (Win32, NT, IP Helper, DXGI) | CI: build, tests, run |

## Design rule: no invented numbers

Every metric is either read from a stable, unprivileged OS interface or reported
as `N/A`. sysmon never substitutes a plausible-looking estimate for a value it
could not measure, and `--json` distinguishes the two cases explicitly: an
unmeasurable metric is `null`, never `0`.

## Features

| Area | What is collected |
|------|-------------------|
| System | Hostname, OS + build, kernel, arch, model, uptime, boot time, timezone, logged-in users, virtualization/container detection |
| CPU | Model, vendor, socket/core/thread topology, P+E core split, per-core usage and frequency, usr/sys/idle/iowait/nice/irq/steal, cache sizes (L1d/L1i/L2/L3), context switches, interrupts, fork rate, instruction-set features, temperature, macOS thermal pressure |
| Memory | RAM used/available/free/cached/buffers, active/inactive/wired/compressed, shared, slab, dirty, commit charge, swap, page-fault and paging rates, memory pressure |
| GPU | Adapter identity, vendor, core count, VRAM total/used/free, clocks, temperature, power |
| Battery | Charge, state, time remaining, health vs design capacity, cycle count, voltage, power draw, temperature |
| Sensors | All temperature sensors with labels and thresholds, fan tachometers, hottest-sensor summary |
| Disk | Per-filesystem usage, inodes, mount options, read-only/removable flags; per-device throughput, IOPS, utilisation and average latency |
| Network | Per-interface throughput, packets, errors, drops, MAC, MTU, link speed, duplex, IPv4/IPv6; default gateway, DNS servers, socket-state census |
| Processes | PID/PPID, user, state, CPU%, accumulated CPU time, RSS/VIRT, threads, nice, open file count and paths, per-process disk I/O rate and lifetime totals, socket count, upload rate, command line |
| Output | Live TUI with nine views and four density levels, one-shot plain text, and full JSON |

### Platform metric availability

Some metrics have no unprivileged, stable API on some platforms. Those are
reported as `N/A` rather than guessed:

| Metric | Linux | macOS | Windows |
|--------|-------|-------|---------|
| CPU usage, per-core usage | ✓ | ✓ | ✓ |
| CPU frequency | ✓ | Intel only (`hw.cpufrequency`) | ✓ |
| CPU temperature | ✓ (hwmon) | N/A — no public die sensor | N/A |
| Thermal pressure level | — | ✓ | — |
| Fan speeds | ✓ (hwmon) | N/A | N/A |
| GPU utilisation | ✓ (amdgpu/i915) | N/A | N/A |
| GPU memory | ✓ | Unified capacity only | ✓ (DXGI) |
| Load average | ✓ | ✓ | N/A — Windows has none |
| Disk I/O latency / utilisation | ✓ | N/A | ✓ |
| Per-process I/O | ✓ | ✓ | ✓ |
| Connections with PID | ✓ | ✓ | ✓ |
| Battery | ✓ | ✓ | ✓ (charge and runtime only) |
| Per-process open files | ✓ (own processes) | ✓ (own processes) | N/A |
| Per-process upload rate | N/A | ✓ | N/A — EStats needs admin |
| Per-process download rate | N/A | N/A — counter double-counts | N/A |
| Per-process socket count | ✓ (own processes) | ✓ | ✓ |

## Quick Start

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/sysmon
```

```bash
./build/sysmon --once
```

```bash
./build/sysmon --json | jq '.cpu.usage_percent, .memory.ram_usage_percent'
```

## Usage

```
Output Modes:
  --once                 Print metrics once and exit
  --no-tui               Plain text output (no ANSI formatting)
  --json                 Emit one JSON snapshot and exit (implies --once)
  --json-compact         As --json, but on a single line
  --compact, -m          Shorthand for --detail compact
  --detail LEVEL         Density: compact, normal, detailed, full
  --view NAME            Start in: overview, cpu, memory, gpu, disk,
                         network, connections, processes, sensors
  --interval N, -i N     Update interval in seconds (default: 2)
  --limit N              Max processes to display (0 = all)
  --all                  Show every process and connection, no limits
  --sort KEY             Process sort: cpu, mem, pid, name, time

Component Visibility Toggles:
  --cores / --no-cores       Show / hide individual CPU cores
  --gpu / --no-gpu           Show / hide GPU & VRAM statistics
  --conn / --no-conn         Show / hide active network connections
  --proc / --no-proc         Show / hide top processes table
  --net / --no-net           Show / hide network interfaces
  --temp / --no-temp         Show / hide temperatures & sensors
  --disk / --no-disk         Show / hide storage & disk I/O
  --battery / --no-battery   Show / hide battery & power
  --net-details              Per-interface MAC, MTU, totals and errors
  --all-interfaces           Include interfaces that carry no traffic
  --listen                   Include listening sockets in connections

Configuration:
  --config PATH          Load configuration from PATH
  --generate-config      Write default config to the config file location
  --show-config          Print the active configuration (a dump, not a config file)
  --version, -v          Show version information
```

Unknown options are rejected with a non-zero exit status rather than ignored.

## Example Output (`--once`)

```
System
  Hostname        archbox
  OS              Arch Linux
  Kernel          6.9.1-arch1
  Architecture    x86_64
  Model           ROG STRIX B650E-F
  Uptime          3h 42m 15s
  Booted          2026-09-07 08:10:11
  Local time      2026-09-07 11:52:26 CEST
  Users           1 logged in
  Processes       312 (1104 threads)

CPU
  Model           AMD Ryzen 9 5950X 16-Core Processor
  Vendor          AuthenticAMD
  Cores           32 logical / 16 physical
  Threads/core    2
  Usage           18.4 %
  usr/sys         14.2 % / 3.8 %
  idle/iowait     81.6 % / 0.2 %
  Frequency       4200 MHz / 4900 MHz max
  Temperature     61.0 °C
  Cache           L1d 512.0 KB  L1i 512.0 KB  L2 8.0 MB  L3 64.0 MB
  Ctx switches    12.4 k/s
  Interrupts      8.9 k/s

Memory
  RAM             18.4 GB / 64.0 GB  (28.7 %)
  Available       45.6 GB
  Cached          12.0 GB
  Swap            0 B / 16.0 GB  (0.0 %)
  Paging          faults 9.0 k/s  major 2/s

Disks
  MOUNTPOINT                FS             USED       SIZE    USE%  INODES
  /                         ext4       200.0 GB     1.0 TB  20.0 %  3.1 %
  /home                     ext4       500.0 GB     2.0 TB  25.0 %  1.2 %

  Disk I/O
  DEVICE                  READ       WRITE        IOPS r/w    UTIL
  nvme0n1              0.0 B/s     4.2 MB/s          0/128     4 %

Network
  Gateway         192.168.1.1
  DNS             192.168.1.1  1.1.1.1
  Sockets         42 established, 7 listening, 3 time-wait

  INTERFACE       ADDRESS                             RX          TX        LINK  STATE
  eth0            192.168.1.100                  1.2 MB/s  240.0 KB/s   1000 Mbps  up

Processes (top 20)
      PID  COMMAND               USER              CPU%    MEM%        RSS       VIRT  THR      TIME  S
     1234  firefox              alice             12.3     3.1   512.0 MB     4.2 GB   64    4m 12s  S
     5678  code                 alice              4.7     1.2   192.0 MB     2.1 GB   28    1m 03s  S
```

## JSON output

`--json` emits every field sysmon collects, including the ones the text and TUI
views omit for space. Unmeasurable metrics are `null`.

It is a data export, not a view: the visibility toggles and the config file's
list limits do not shape it, so a section is never empty merely because the
dashboard was configured to hide it, and process and connection lists are never
silently truncated. An explicit `--limit N` is still honoured.

```bash
sysmon --json | jq '.network.interfaces[] | select(.is_up) | {name, speed_mbps, rx_bytes_per_sec}'
sysmon --json | jq '.processes | sort_by(-.mem_rss_bytes) | .[0:5] | .[].name'
sysmon --json-compact >> metrics.ndjson   # one line per sample, append-friendly
```

## Views

The dashboard opens on an overview of every section. Pressing a digit switches
to a full-screen view of one subsystem, which shows the fields the overview has
no room for and, where the list is long, scrolls.

| Key | View | What it adds over the overview |
|-----|------|-------------------------------|
| `0` | Overview | Every enabled section, as before |
| `1` | CPU | Topology, P/E clusters, cache sizes, all cores (scrollable), context switches, instruction set |
| `2` | Memory | Active/inactive/wired/compressed, slab, dirty, commit charge, paging and swap rates, largest consumers |
| `3` | GPU | Per adapter: driver, cores, VRAM, core and memory clocks, encoder/decoder, power, fan |
| `4` | Disk | Inodes, mount options, per-device IOPS, utilisation, latency, queue depth, and the processes doing the I/O |
| `5` | Network | Per interface MAC, MTU, duplex, link speed, totals, errors and drops; gateway, DNS, upload by process |
| `6` | Connections | Every socket with its owning process, scrollable |
| `7` | Processes | The full process table, scrollable, with disk I/O and CPU time columns |
| `8` | Sensors | Every temperature sensor with its thresholds, fans, and battery detail |

Inside a list view, `Enter` opens an inspector for the selected process: its
full command line, accumulated CPU time, cumulative and current disk I/O, its
own connections, and the files it has open.

## Density

Four levels, changed live with `+` and `-` or set with `--detail`:

| Level | Meaning |
|-------|---------|
| `compact` | One line per subsystem — fits a small pane |
| `normal` | The default dashboard |
| `detailed` | Every field a section has a layout for |
| `full` | Detailed plus the long tails: all cores, all sensors, CPU flags, command lines |

The level applies to the plain-text output too, so `--once --no-tui --detail full`
prints everything sysmon can lay out as text.

## Keyboard Shortcuts (TUI Mode)

| Key | Action |
|-----|--------|
| `0`–`8` | Switch to that view |
| `Tab` | Next view |
| `Esc` | Back to the overview; quits from the overview |
| `+` / `-` | More / less detail |
| `m` | Toggle compact density |
| `↑` `↓` / `k` `j` | Move the cursor in a list |
| `PgUp` / `PgDn` | Page through a list |
| `Home` / `End` | Jump to the start / end |
| `Enter` | Inspect the selected process |
| `a` | Show all — lift the process and connection limits |
| `c` | Toggle per-core CPU |
| `g` | Toggle GPU |
| `n` | Toggle network |
| `v` | Toggle connections |
| `p` | Toggle processes |
| `t` | Toggle temperatures |
| `d` | Toggle storage |
| `b` | Toggle battery |
| `o` | Cycle process sort order |
| `s` | Save current settings to config |
| `r` | Force refresh |
| `q` / `Ctrl+C` | Quit |

## Building

### Dependencies

- C++20 compiler (GCC 10+, Clang 12+, AppleClang, MSVC 2019+)
- CMake 3.16+
- No external runtime libraries

### Linux

```bash
sudo apt install cmake g++          # Debian/Ubuntu
sudo pacman -S cmake gcc            # Arch Linux
```

### macOS

```bash
xcode-select --install
brew install cmake
```

### Windows

Visual Studio 2019 or newer with the C++ workload, or MinGW-w64.

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\sysmon.exe
```

### Compile

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
sudo cmake --install build      # install to /usr/local/bin
```

### Single-file builds

```bash
g++ -std=c++20 -O2 -I include -I build/include src/*.cpp -o sysmon
```

```bash
clang++ -std=c++20 -O2 -I include -I build/include src/*.cpp \
    -framework IOKit -framework CoreFoundation -o sysmon
```

```bash
x86_64-w64-mingw32-g++ -std=c++20 -O2 -I include -I build/include src/*.cpp \
    -o sysmon.exe -liphlpapi -lpsapi -lws2_32 -lpowrprof -lwtsapi32 -ldxgi -lole32 -static
```

## Tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Configuration

sysmon reads an INI-style config file:

- Linux / macOS: `$XDG_CONFIG_HOME/sysmon/sysmon.conf`, else `~/.config/sysmon/sysmon.conf`
- Windows: `%APPDATA%\sysmon\sysmon.conf`

```bash
sysmon --generate-config
```

## Documentation

Full documentation is at **[sysmon.readthedocs.io](https://sysmon.readthedocs.io)**.

```bash
cd docs
pip install -r requirements.txt
sphinx-build source _build/html
```

## Architecture

```
main.cpp
   │
   ├─ terminal.hpp ......... POSIX termios / Windows console abstraction
   │
   ├─ [Monitors] ─────────► Snapshot ─────────► [Renderers]
   │                                              ├─ TUI (ANSI dashboard)
   │   SystemMonitor    DiskMonitor               ├─ TextRenderer (plain text)
   │   CpuMonitor       DiskIOMonitor             └─ JsonRenderer (machine readable)
   │   MemoryMonitor    NetworkMonitor
   │   GpuMonitor       NetConnectionsMonitor
   │   LoadMonitor      ProcessMonitor
   │   BatteryMonitor   TemperatureMonitor
```

Each monitor owns its platform `#if` branches and returns the same struct on
every platform, so the renderers are entirely platform independent.

See [Architecture documentation](docs/source/architecture.md) for details.

## License

MIT
