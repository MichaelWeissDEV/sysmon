# Stats Reference

All data structures are defined in `include/sysmon/stats.hpp`.

## SystemStats

| Field | Type | Description |
|-------|------|-------------|
| `hostname` | `string` | Machine hostname |
| `os` | `string` | Pretty OS name |
| `kernel` | `string` | Kernel version string |
| `uptime` | `string` | Human-readable uptime (e.g. "3d 2h 15m") |
| `uptime_seconds` | `double` | Uptime in raw seconds |
| `architecture` | `string` | CPU architecture (e.g. "x86_64") |

## CpuStats

See [CpuMonitor](cpu_monitor).

## MemoryStats

| Field | Type | Description |
|-------|------|-------------|
| `ram_total_bytes` | `uint64_t` | Total physical RAM |
| `ram_used_bytes` | `uint64_t` | Used RAM (total − available) |
| `ram_available_bytes` | `uint64_t` | Available RAM |
| `ram_cached_bytes` | `uint64_t` | Page cache |
| `ram_buffer_bytes` | `uint64_t` | Kernel buffers |
| `swap_total_bytes` | `uint64_t` | Total swap space |
| `swap_used_bytes` | `uint64_t` | Used swap space |
| `ram_usage_percent` | `double` | RAM usage [0..100] |
| `swap_usage_percent` | `double` | Swap usage [0..100] |

## LoadStats

| Field | Type | Description |
|-------|------|-------------|
| `load_1min` | `double` | 1-minute load average |
| `load_5min` | `double` | 5-minute load average |
| `load_15min` | `double` | 15-minute load average |
| `running_processes` | `unsigned int` | Currently running processes |
| `total_processes` | `unsigned int` | Total process count |

## DiskStats

| Field | Type | Description |
|-------|------|-------------|
| `mountpoint` | `string` | Mount point (e.g. "/", "/home") |
| `device` | `string` | Device node (e.g. "/dev/sda1") |
| `filesystem_type` | `string` | FS type (e.g. "ext4", "apfs") |
| `total_bytes` | `uint64_t` | Total filesystem capacity |
| `used_bytes` | `uint64_t` | Used bytes |
| `available_bytes` | `uint64_t` | Available bytes |
| `usage_percent` | `double` | Used percentage [0..100] |

## DiskIOStats

| Field | Type | Description |
|-------|------|-------------|
| `device` | `string` | Block device name (e.g. "sda") |
| `read_bytes_per_sec` | `double` | Read throughput in B/s |
| `write_bytes_per_sec` | `double` | Write throughput in B/s |
| `read_ops_per_sec` | `double` | Read IOPS |
| `write_ops_per_sec` | `double` | Write IOPS |

## NetworkStats

See [NetworkMonitor](network_monitor).

## ProcessStats

| Field | Type | Description |
|-------|------|-------------|
| `pid` | `int` | Process ID |
| `name` | `string` | Process name |
| `user` | `string` | Username |
| `state` | `string` | State char (R=running, S=sleeping, …) |
| `cpu_percent` | `double` | CPU usage percentage |
| `mem_rss_bytes` | `uint64_t` | Resident set size |
| `mem_vms_bytes` | `uint64_t` | Virtual memory size |
| `mem_percent` | `double` | Memory as % of total RAM |
| `threads` | `unsigned int` | Thread count |
| `start_time` | `long long` | Start time (seconds since boot) |

## TemperatureStats / SensorReading

See [TemperatureMonitor](temperature_monitor).
