# TemperatureMonitor

## Overview

`TemperatureMonitor` reads all available hardware temperature sensors.

**Header:** `include/sysmon/temperature_monitor.hpp`  
**Source:** `src/temperature_monitor.cpp`

## Class

```cpp
class TemperatureMonitor {
public:
    TemperatureStats      read();
    std::optional<double> read_cpu_temperature();
};
```

## `TemperatureStats`

| Field | Type | Description |
|-------|------|-------------|
| `sensors` | `vector<SensorReading>` | All available sensor readings |
| `cpu_package` | `optional<double>` | CPU package / Tdie temperature |

## `SensorReading`

| Field | Type | Description |
|-------|------|-------------|
| `name` | `std::string` | Sensor label (e.g. "coretemp: Core 0") |
| `temperature_celsius` | `double` | Current temperature in °C |
| `critical` | `optional<double>` | Critical threshold in °C (if known) |
| `high` | `optional<double>` | High threshold in °C (if known) |

## Platform Notes

| Platform | Data Source |
|----------|-------------|
| Linux | `/sys/class/hwmon/hwmon*/temp*_input`, `/sys/class/thermal/thermal_zone*/temp` |
| macOS | IOKit `AppleSmartBattery` (battery temperature); CPU temperature is `N/A` |

### Linux Details

- **hwmon** sensors: reads all `temp<N>_input` files in each `/sys/class/hwmon/hwmonN/` directory.
  Labels come from `temp<N>_label`; thresholds from `temp<N>_max` and `temp<N>_crit`.
- **CPU package** is auto-detected from `coretemp`, `k10temp`, and `zenpower` drivers.
- **Fallback**: `/sys/class/thermal/thermal_zone*/temp` is used if hwmon is unavailable.

### macOS Details

- The CPU/SOC package temperature is not exposed through any stable, unprivileged
  macOS API. `cpu_package` is therefore reported as `N/A`. Thermal-pressure levels
  are **not** converted into a fake temperature.
- Battery temperature is read from the `AppleSmartBattery` IOKit service
  (reported in tenths of a degree Celsius) and only accepted after a
  plausibility check.

## Example

```cpp
TemperatureMonitor mon;
auto temps = mon.read();

if (temps.cpu_package.has_value()) {
    std::cout << "CPU: " << temps.cpu_package.value() << " °C\n";
}

for (const auto& s : temps.sensors) {
    std::cout << s.name << ": " << s.temperature_celsius << " °C";
    if (s.critical.has_value()) {
        std::cout << " (crit: " << s.critical.value() << " °C)";
    }
    std::cout << "\n";
}
```
