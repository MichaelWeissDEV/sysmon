# Config

## Overview

`Config` manages all runtime settings, configuration file I/O, default properties, and section filtering.

**Header:** `include/sysmon/config.hpp`  
**Source:** `src/config.cpp`

## Methods

```cpp
struct Config {
    // Static Loaders
    static Config load();
    static Config load_from(const std::string& path);
    static Config defaults();
    static std::string default_config_path();

    // Exporters
    void save() const;
    void save_to(const std::string& path) const;
    std::string to_string() const;
    DisplayFlags to_display_flags() const;
};
```

## Settings Reference

| Key | Type | Default | Description |
|---|---|---|---|
| `refresh_interval` | `int` | `2` | Refresh period in seconds |
| `tui_enabled` | `bool` | `true` | Enable interactive ANSI TUI |
| `compact_mode` | `bool` | `false` | Enable compact summary mode |
| `show_cpu` | `bool` | `true` | Show aggregate CPU |
| `show_cpu_per_core` | `bool` | `true` | Show per-core CPU bars |
| `show_gpu` | `bool` | `true` | Show GPU / graphics processor |
| `show_gpu_memory` | `bool` | `true` | Show VRAM or Unified Memory |
| `show_memory` | `bool` | `true` | Show RAM memory |
| `show_swap` | `bool` | `true` | Show Swap memory |
| `show_disk` | `bool` | `true` | Show storage mounts |
| `show_disk_io` | `bool` | `true` | Show Disk read/write throughput |
| `show_network` | `bool` | `true` | Show network interfaces & speed |
| `show_connections` | `bool` | `true` | Show active network sockets |
| `show_processes` | `bool` | `true` | Show top process list |
| `show_temperature` | `bool` | `true` | Show hardware temperatures |
| `proc_limit` | `int` | `20` | Max processes to render |
| `connections_limit` | `int` | `30` | Max connections to render |
| `excluded_interfaces` | `std::set<std::string>` | `{"lo", "lo0"}` | Hidden network interfaces |
| `excluded_filesystems` | `std::set<std::string>` | `{}` | Hidden filesystem types |
| `excluded_sensors` | `std::set<std::string>` | `{}` | Hidden sensor labels |
