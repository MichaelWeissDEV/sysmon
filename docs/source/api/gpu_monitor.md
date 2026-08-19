# GpuMonitor

## Overview

`GpuMonitor` collects statistics for dedicated and integrated graphics processing units across Linux and macOS (Apple Silicon).

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
| `name` | `std::string` | Device name (e.g., "Apple M4 Max GPU", "NVIDIA GeForce RTX 4090") |
| `vendor` | `std::string` | Vendor name: "Apple", "NVIDIA", "AMD", "Intel" |
| `gpu_cores` | `unsigned int` | Number of GPU compute / shader cores |
| `memory_type` | `std::string` | Memory architecture: "Unified", "GDDR6X", "Shared", etc. |
| `memory_total_bytes` | `uint64_t` | Total VRAM or Unified Memory in bytes |
| `memory_used_bytes` | `uint64_t` | Used VRAM / GPU memory in bytes |
| `memory_free_bytes` | `uint64_t` | Available VRAM in bytes |
| `usage_percent` | `double` | GPU engine utilization percentage [0..100] |
| `memory_usage_percent` | `double` | VRAM usage percentage [0..100] |
| `frequency_mhz` | `double` | Core clock speed in MHz |
| `temperature_celsius` | `std::optional<double>` | GPU temperature in °C |
| `power_watts` | `std::optional<double>` | Power draw in Watts |

## Platform Support

### Apple Silicon (macOS)
- Reads processor brand and architecture to detect Apple M1/M2/M3/M4 series chips.
- Detects GPU core counts (e.g. 40 cores on M4 Max, 80 cores on M4 Ultra).
- Measures Unified Memory sharing directly from Mach VM statistics (`vm_statistics64`).

### Linux (AMD, NVIDIA, Intel)
- **AMD:** Reads GPU load, VRAM allocations, temperatures, and power from `/sys/class/drm/card*/device/` via the `amdgpu` driver.
- **NVIDIA:** Reads clock frequencies, power, and temperatures via NVIDIA sysfs / DRM interfaces.
- **Intel:** Monitors integrated GPU clock speeds and activity from the `i915` kernel driver.

## Example

```cpp
#include "sysmon/gpu_monitor.hpp"
#include <iostream>

int main() {
    GpuMonitor mon;
    auto gpus = mon.read();

    for (const auto& g : gpus) {
        std::cout << g.name << " (" << g.vendor << ")\n";
        std::cout << "  Cores: " << g.gpu_cores << "\n";
        std::cout << "  Memory: " << g.memory_used_bytes / (1024*1024) << " MB / "
                  << g.memory_total_bytes / (1024*1024) << " MB (" << g.memory_type << ")\n";
    }
    return 0;
}
```
