# Architecture

## Overview

sysmon follows a clean **collect → render** separation. Each monitor class is
responsible for a single data source and owns all of its platform `#if` branches,
returning the same struct on every platform. One refresh produces a single
`Snapshot` (defined in `stats.hpp`) that is handed to a renderer, so the entire
output layer is platform independent.

```
┌──────────────────────────────────────────────────────────┐
│                        main.cpp                          │
│  Creates monitors, runs the refresh loop, handles keys   │
└────────────┬─────────────────────────────────────────────┘
             │
             │  terminal.hpp — termios / Windows console abstraction
             │  (raw key input, size, resize detection, ANSI enablement)
             │
             │  calls read() on each monitor
             ▼
┌────────────────────────────────────────────────────────────────────────────┐
│  Monitors (src/  include/sysmon/)                                          │
│                                                                            │
│  SystemMonitor   CpuMonitor      MemoryMonitor    LoadMonitor              │
│  DiskMonitor     DiskIOMonitor   TemperatureMonitor  BatteryMonitor        │
│  GpuMonitor      NetworkMonitor  NetConnectionsMonitor  ProcessMonitor     │
│                                                                            │
│  Each owns its own #if SYSMON_LINUX / MACOS / WINDOWS branches.            │
└────────────────┬───────────────────────────────────────────────────────────┘
                 │  returns one populated Snapshot (stats.hpp)
                 ▼
┌────────────────────────────────────────────┐
│  Output layer (fully platform independent) │
│                                            │
│  TUI            — ANSI live dashboard      │
│  TextRenderer   — plain text tables        │
│  JsonRenderer   — machine-readable output  │
└────────────────────────────────────────────┘
```

---

## Cross-platform strategy

Three rules keep the platform-specific code contained:

1. **One struct per subsystem, filled differently per platform.** A monitor's
   header never changes shape between platforms; only its `.cpp` has `#if`
   branches. The renderers therefore never test the platform.

2. **Unmeasurable is a value, not a zero.** Anything a platform cannot read
   through a stable, unprivileged API is an empty `std::optional`, rendered as
   `N/A` in text and `null` in JSON. This is what keeps a platform gap from
   looking like a measurement.

3. **All terminal access goes through `terminal.hpp`.** `main.cpp` and `tui.cpp`
   contain no `termios.h` and no `windows.h`. On Windows the layer additionally
   switches the console to UTF-8 and turns on
   `ENABLE_VIRTUAL_TERMINAL_PROCESSING`, which the entire ANSI output depends on.

### Per-platform data sources

| Subsystem | Linux | macOS | Windows |
|-----------|-------|-------|---------|
| CPU | `/proc/stat`, `/proc/cpuinfo`, sysfs cpufreq | `host_processor_info`, sysctl | `NtQuerySystemInformation`, `CallNtPowerInformation`, registry |
| Memory | `/proc/meminfo`, `/proc/vmstat` | `host_statistics64`, `vm.swapusage` | `GlobalMemoryStatusEx`, `GetPerformanceInfo` |
| Load | `/proc/loadavg`, `/proc` walk | `getloadavg`, `KERN_PROC_ALL` + `proc_pidinfo` | Toolhelp32 census (no load average exists) |
| Disks | `/proc/mounts`, `statvfs` | `getmntinfo` | `GetLogicalDriveStrings`, `GetDiskFreeSpaceEx` |
| Disk I/O | `/proc/diskstats` | IOKit `IOBlockStorageDriver` | `IOCTL_DISK_PERFORMANCE` |
| Network | `/proc/net/dev`, sysfs, `getifaddrs` | `getifaddrs` `AF_LINK`, `SIOCGIFMEDIA` | `GetIfTable2`, `GetAdaptersAddresses` |
| Connections | `/proc/net/tcp*` + fd scan | `netstat -anv` | `GetExtendedTcpTable` |
| Processes | `/proc/<pid>/*` | `KERN_PROC_ALL`, `proc_pidinfo`, `proc_pid_rusage` | Toolhelp32, PSAPI |
| GPU | sysfs DRM | sysctl | DXGI |
| Battery | `/sys/class/power_supply` | IOKit `AppleSmartBattery` | `GetSystemPowerStatus` |
| Sensors | hwmon, thermal zones | IOKit battery thermistor | not available |

---

## Rate calculation

Every "per second" figure is a delta between two samples, never an average since
boot. Each monitor keeps the previous counter snapshot and its timestamp, and:

- a counter that moved **backwards** (a reset, a wrap, or a device that
  disappeared and came back) yields `0`, not a spike;
- the **first** call after startup produces no rate at all, which is why
  `main.cpp` takes a warm-up sample before the first frame is drawn.

---

## File Structure

```
sysmon/
├── CMakeLists.txt          # Build system
├── README.md
├── include/
│   └── sysmon/
│       ├── platform.hpp    # Platform detection macros
│       ├── terminal.hpp    # termios / Windows console abstraction
│       ├── stats.hpp       # All data structures, incl. Snapshot
│       ├── system_monitor.hpp
│       ├── cpu_monitor.hpp
│       ├── memory_monitor.hpp
│       ├── load_monitor.hpp
│       ├── disk_monitor.hpp
│       ├── disk_io_monitor.hpp
│       ├── temperature_monitor.hpp
│       ├── network_monitor.hpp
│       ├── net_connections_monitor.hpp
│       ├── process_monitor.hpp
│       ├── battery_monitor.hpp
│       ├── gpu_monitor.hpp
│       ├── tui.hpp         # ANSI TUI dashboard
│       ├── text_renderer.hpp
│       ├── json_renderer.hpp
│       └── utils.hpp
├── src/
│   ├── main.cpp            # Entry point + live loop
│   ├── system_monitor.cpp
│   ├── cpu_monitor.cpp
│   ├── memory_monitor.cpp
│   ├── load_monitor.cpp
│   ├── disk_monitor.cpp
│   ├── disk_io_monitor.cpp
│   ├── temperature_monitor.cpp
│   ├── network_monitor.cpp
│   ├── process_monitor.cpp
│   ├── tui.cpp
│   ├── text_renderer.cpp
│   └── utils.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── test_utils.cpp
│   ├── test_cpu_monitor.cpp
│   ├── test_memory_monitor.cpp
│   ├── test_load_monitor.cpp
│   ├── test_system_monitor.cpp
│   ├── test_network_monitor.cpp
│   ├── test_disk_monitor.cpp
│   ├── test_disk_io_monitor.cpp
│   ├── test_temperature_monitor.cpp
│   ├── test_process_monitor.cpp
│   ├── test_net_connections_monitor.cpp
│   ├── test_text_renderer.cpp
│   ├── test_cli.cpp
│   └── test_config.cpp
└── docs/
    ├── source/             # Sphinx documentation (Read the Docs)
    └── requirements.txt
```

---

## Data Flow

```
/proc/stat         → CpuMonitor         → CpuStats
/proc/cpuinfo      →                    → (model, cores, freq)
/sys/class/hwmon/  → TemperatureMonitor → TemperatureStats
/proc/meminfo      → MemoryMonitor      → MemoryStats
/proc/loadavg      → LoadMonitor        → LoadStats
/proc/mounts       → DiskMonitor        → [DiskStats]
  statvfs()        →
/proc/diskstats    → DiskIOMonitor      → [DiskIOStats]
/proc/net/dev      → NetworkMonitor     → [NetworkStats]
  getifaddrs()     →
/proc/<pid>/stat   → ProcessMonitor     → [ProcessStats]

macOS:
  sysctl()         → SystemMonitor, CpuMonitor, LoadMonitor
  mach/host_*()    → CpuMonitor, MemoryMonitor
  getmntinfo()     → DiskMonitor
  task_info()      → ProcessMonitor
  getifaddrs()     → NetworkMonitor
```

---

## Adding a New Monitor

1. Define a new `XxxStats` struct in `include/sysmon/stats.hpp`
2. Create `include/sysmon/xxx_monitor.hpp` with `XxxStats read();`
3. Implement Linux and macOS code paths in `src/xxx_monitor.cpp`
4. Add the source to `CMakeLists.txt`
5. Instantiate the monitor in `main.cpp` and pass data to `TUI::render()` / `TextRenderer::render()`
6. Add a test in `tests/test_xxx_monitor.cpp`
