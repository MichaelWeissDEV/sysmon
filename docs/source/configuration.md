# Configuration & Customization

`sysmon` allows you to customize every aspect of the monitoring dashboard. You can configure what components are shown, set thresholds, filter network interfaces or file systems, and toggle between detailed deep monitoring or a compact average summary dashboard.

## Configuration File

The configuration file is resolved in the following order:

1. `$XDG_CONFIG_HOME/sysmon/sysmon.conf`
2. `~/.config/sysmon/sysmon.conf`

On macOS and other systems without `XDG_CONFIG_HOME`, the `~/.config` location is used.

You can generate a default configuration file with all available options by running:
```bash
sysmon --generate-config
```

To view the currently active configuration in your terminal:
```bash
sysmon --show-config
```

### Complete `sysmon.conf` Example

```ini
# sysmon configuration file
# Edit this file to customize what sysmon displays.

[display]
refresh_interval = 2
tui_enabled = true
compact_mode = false

# CPU Options
show_cpu = true
show_cpu_per_core = true
show_cpu_cores_detail = true

# Memory Options
show_memory = true
show_swap = true
show_memory_cache = true

# GPU & Graphics Options (Apple Silicon, NVIDIA, AMD, Intel)
show_gpu = true
show_gpu_memory = true

# Temperature & Sensor Options
show_temperature = true
show_temperature_per_sensor = true

# Storage & Disk I/O Options
show_disk = true
show_disk_io = true

# Network Traffic Options
show_network = true
show_network_per_iface = true
show_network_sparkline = true

# Network Connections & Sockets
show_connections = true
connections_limit = 30
connections_show_listen = false

# Process List Options
show_processes = true
proc_limit = 20
show_proc_threads = true
show_proc_network = false

[tui]
use_unicode = true
use_colors = true
sparkline_length = 40
proc_sort_col = 0

[network]
# Comma-separated list of interface names to hide
exclude_interfaces = lo, lo0, docker0, veth*

[disk]
# Comma-separated list of filesystem types to hide
exclude_filesystems = tmpfs, devtmpfs, squashfs, overlay

[temperature]
# Comma-separated list of sensor names/chips to exclude
exclude_sensors = thermal-zone3
```

---

## Interactive Hotkeys in Live TUI

While running the live TUI dashboard, you can toggle components and modes instantly on the fly:

| Key | Function |
|---|---|
| `c` | Toggle individual CPU core bars |
| `g` | Toggle GPU / Graphics stats |
| `n` | Toggle Network interface bandwidth & sparklines |
| `v` | Toggle Active Network Connections table |
| `p` | Toggle Process list table |
| `t` | Toggle Temperatures & hardware sensors |
| `d` | Toggle Storage & Disk I/O read/write rates |
| `m` | Toggle **Compact Mode** (switches between full deep view and summary dashboard) |
| `s` | **Save** current interactive view settings to `~/.config/sysmon/sysmon.conf` |
| `r` | Force immediate refresh |
| `q` / `ESC` | Quit sysmon |

---

## Command-Line Arguments

You can also override any configuration setting using CLI flags:

### Display & Modes
- `sysmon --once` : Output snapshot once to stdout and exit (ideal for scripts and cron)
- `sysmon --no-tui` : Stream plain text updates without ANSI escape codes
- `sysmon --compact` or `sysmon -m` : Start in compact summary mode
- `sysmon --interval N` or `sysmon -i N` : Set update frequency in seconds (e.g. `-i 1`)
- `sysmon --limit N` : Set max number of processes to display

### Component Toggles
- `--cores` / `--no-cores` : Enable / disable per-core CPU breakdown
- `--gpu` / `--no-gpu` : Enable / disable GPU monitoring
- `--conn` / `--no-conn` : Enable / disable active network connections
- `--proc` / `--no-proc` : Enable / disable process list
- `--net` / `--no-net` : Enable / disable network interfaces
- `--temp` / `--no-temp` : Enable / disable temperatures
- `--disk` / `--no-disk` : Enable / disable storage and disk I/O

### Configuration File Options
- `--config /path/to/file.conf` : Load custom configuration file
- `--generate-config` : Create default config at the config file location
- `--show-config` : Print current parsed configuration
