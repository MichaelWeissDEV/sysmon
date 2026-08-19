# GpuMonitor

## Overview

`GpuMonitor` collects statistics for graphics processing units on Linux and macOS (Apple Silicon).

**Header:** `include/sysmon/gpu_monitor.hpp`  
**Source:** `src/gpu_monitor.cpp`

## Class Definition

```cpp
class GpuMonitor {
public:
    GpuMonitor();
    std::vector<GpuStats> read();
};
```

## `GpuStats`

| Field | Type | Description |
|---|---|---|
| `name` | `std::string` | Device name (e.g. "Apple M4 Max GPU") |
| `vendor` | `std::string` | Vendor name: "Apple", "NVIDIA", "AMD", "Intel" |
| `gpu_cores` | `std::optional<unsigned int>` | Number of GPU compute / shader cores |
| `memory_type` | `std::string` | Memory architecture: "Unified", "GDDR", "Shared", etc. |
| `memory_total_bytes` | `std::optional<uint64_t>` | Total VRAM or Unified Memory in bytes |
| `memory_used_bytes` | `std::optional<uint64_t>` | Used VRAM / GPU memory in bytes |
| `memory_free_bytes` | `std::optional<uint64_t>` | Available VRAM in bytes |
| `usage_percent` | `std::optional<double>` | GPU engine utilization percentage [0..100] |
| `memory_usage_percent` | `std::optional<double>` | VRAM usage percentage [0..100] |
| `frequency_mhz` | `std::optional<double>` | Core clock speed in MHz |
| `temperature_celsius` | `std::optional<double>` | GPU temperature in °C |
| `power_watts` | `std::optional<double>` | Power draw in Watts |

> Fields that cannot be measured on a given platform are reported as `std::nullopt` and rendered as `N/A`; they are never reported as `0`.

## Platform Support

### macOS (Apple Silicon)
- Reports the vendor (`Apple`) and a device name derived from `machdep.cpu.brand_string` (e.g. "Apple M4 Max GPU").
- `gpu_cores` is populated only from the `hw.gpu.count` sysctl when the kernel exposes it.
- `memory_total_bytes` reports the system unified-memory capacity (`hw.memsize`); it is labeled as system capacity, not GPU-specific allocation.
- GPU usage, clock frequency, and GPU memory usage are **not** published by Apple via an unprivileged, stable API, so they are `N/A`. No hard-coded chip lookup tables and no derived values are used.

### Linux (AMD, NVIDIA, Intel)
- Best-effort reading from `/sys/class/drm/card*/device/`:
  - **AMD (`amdgpu`):** `gpu_busy_percent` (usage), `mem_info_vram_total`/`mem_info_vram_used` (VRAM), hwmon temperature/power/frequency.
  - **NVIDIA:** hwmon temperature, `gt_cur_freq_mhz` when present.
  - **Intel (`i915`):** `gt_act_freq_mhz`/`gt_cur_freq_mhz`, hwmon temperature.
- Missing sysfs files are skipped gracefully.

## Example

```cpp
#include "sysmon/gpu_monitor.hpp"
#include <iostream>

int main() {
    GpuMonitor mon;
    auto gpus = mon.read();

    for (const auto& g : gpus) {
        std::cout << g.name << " (" << g.vendor << ")\n";
        if (g.memory_total_bytes.has_value()) {
            std::cout << "  Memory: " << *g.memory_total_bytes / (1024*1024)
                      << " MB (" << g.memory_type << ")\n";
        }
    }
    return 0;
}
```