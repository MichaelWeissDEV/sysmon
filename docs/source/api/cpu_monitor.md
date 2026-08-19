# CpuMonitor

## Overview

`CpuMonitor` collects CPU usage, frequency, core count, and temperature.

**Header:** `include/sysmon/cpu_monitor.hpp`  
**Source:** `src/cpu_monitor.cpp`

## Class

```cpp
class CpuMonitor {
public:
    CpuMonitor();
    CpuStats read();
};
```

## `CpuStats`

| Field | Type | Description |
|-------|------|-------------|
| `model` | `std::string` | CPU brand string (e.g. "AMD Ryzen 9 5950X") |
| `logical_cores` | `unsigned int` | Total logical CPUs (threads) |
| `physical_cores` | `unsigned int` | Physical core count |
| `usage_percent` | `double` | Overall CPU usage [0..100], where 100 % = one fully utilized logical core |
| `frequency_mhz` | `optional<double>` | Current clock speed in MHz (N/A when unavailable) |
| `max_frequency_mhz` | `optional<double>` | Maximum/base clock speed in MHz (N/A when unavailable) |
| `temperature_celsius` | `optional<double>` | CPU package temperature (if available) |
| `user_percent` | `double` | User-space CPU time percentage |
| `system_percent` | `double` | Kernel CPU time percentage |
| `iowait_percent` | `double` | I/O wait percentage (Linux only) |
| `idle_percent` | `double` | Idle percentage |
| `per_core` | `vector<CoreStats>` | Per-logical-core breakdown |

## `CoreStats`

| Field | Type | Description |
|-------|------|-------------|
| `id` | `unsigned int` | Zero-based core index |
| `usage_percent` | `double` | Core usage [0..100] |
| `frequency_mhz` | `optional<double>` | Core-specific frequency (Linux only; N/A otherwise) |

## Platform Notes

| Platform | Data Source |
|----------|-------------|
| Linux | `/proc/stat`, `/proc/cpuinfo`, `/sys/devices/system/cpu/*/cpufreq/` |
| macOS | `sysctl()`, `host_processor_info()` (Mach API) |

### macOS details

- Frequencies are only reported when `hw.cpufrequency` / `hw.cpufrequency_max` /
  `hw.cpufrequency_min` return valid values. Apple Silicon does not expose these,
  so frequency is `N/A`.
- Usage follows the same convention as Linux: 100 % = one fully utilized logical core.
- Counter resets (e.g. sleep/wake) are handled: a negative delta yields 0 % usage.

## Example

```cpp
CpuMonitor mon;
mon.read();  // warm-up call (fills internal prev-sample state)

std::this_thread::sleep_for(std::chrono::seconds(1));

CpuStats stats = mon.read();
std::cout << "CPU: " << stats.usage_percent << "% @ "
          << stats.frequency_mhz << " MHz\n";

for (const auto& core : stats.per_core) {
    std::cout << "Core " << core.id << ": " << core.usage_percent << "%\n";
}
```
