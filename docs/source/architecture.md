# Architecture

## Overview

sysmon follows a clean **collect → render** separation. Each monitor class is responsible for a single data source. The collected data is stored in plain structs (defined in `stats.hpp`) and passed to either the `TUI` or `TextRenderer` for output.

```
┌──────────────────────────────────────────────────────────┐
│                        main.cpp                          │
│  Creates monitors, runs the refresh loop, handles keys   │
└────────────┬─────────────────────────────────────────────┘
             │  calls read() on each monitor
             ▼
┌────────────────────────────────────────────────────────────────────────────┐
│  Monitors (src/  include/sysmon/)                                          │
│                                                                            │
│  SystemMonitor   CpuMonitor    MemoryMonitor    LoadMonitor                │
│  DiskMonitor     DiskIOMonitor TemperatureMonitor                          │
│  GpuMonitor      NetworkMonitor NetConnectionsMonitor ProcessMonitor       │
└────────────────┬───────────────────────────────────────────────────────────┘
                 │  returns populated structs (stats.hpp)
                 ▼
┌───────────────────────────────────────┐
│  Output layer                         │
│                                       │
│  TUI            — ANSI live dashboard │
│  TextRenderer   — plain text fallback │
└───────────────────────────────────────┘
```

---

## File Structure

```
sysmon/
├── CMakeLists.txt          # Build system
├── README.md
├── include/
│   └── sysmon/
│       ├── platform.hpp    # Platform detection macros
│       ├── stats.hpp       # All data structures
│       ├── system_monitor.hpp
│       ├── cpu_monitor.hpp
│       ├── memory_monitor.hpp
│       ├── load_monitor.hpp
│       ├── disk_monitor.hpp
│       ├── disk_io_monitor.hpp
│       ├── temperature_monitor.hpp
│       ├── network_monitor.hpp
│       ├── process_monitor.hpp
│       ├── tui.hpp         # ANSI TUI dashboard
│       ├── text_renderer.hpp
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
