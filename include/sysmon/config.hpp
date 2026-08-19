/**
 * @file config.hpp
 * @brief sysmon configuration system.
 *
 * Loads / saves a simple INI-style config file at
 * $XDG_CONFIG_HOME/sysmon/sysmon.conf (falling back to
 * ~/.config/sysmon/sysmon.conf).  No external library dependencies.
 */

#ifndef SYSMON_CONFIG_HPP
#define SYSMON_CONFIG_HPP

#include "sysmon/stats.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>

/**
 * @brief All configurable sysmon settings.
 *
 * Sensible defaults are provided for every field.  Users can override by
 * editing $XDG_CONFIG_HOME/sysmon/sysmon.conf (or ~/.config/...) or via CLI flags.
 */
struct Config {

    // ------------------------------------------------------------------
    // [display]
    // ------------------------------------------------------------------
    int  refresh_interval{2};       ///< Seconds between TUI/CLI refreshes
    bool tui_enabled{true};         ///< Use live TUI (false = one-shot)
    bool compact_mode{false};       ///< Condensed single-line sections

    // CPU
    bool show_cpu{true};
    bool show_cpu_per_core{true};   ///< Individual core bars
    bool show_cpu_cores_detail{true}; ///< Show usr/sys/iowait breakdown

    // Memory
    bool show_memory{true};
    bool show_swap{true};
    bool show_memory_cache{true};

    // GPU
    bool show_gpu{true};
    bool show_gpu_memory{true};
    bool show_gpu_per_core{false};  ///< Reserved for future use

    // Temperature
    bool show_temperature{true};
    bool show_temperature_per_sensor{true};
    std::set<std::string> excluded_sensors;  ///< Sensor names to hide

    // Disk
    bool show_disk{true};
    bool show_disk_io{true};
    std::set<std::string> excluded_filesystems; ///< e.g. "tmpfs", "devtmpfs"

    // Network
    bool show_network{true};
    bool show_network_per_iface{true};
    bool show_network_sparkline{true};
    std::set<std::string> excluded_interfaces; ///< e.g. "lo", "lo0"

    // Connections
    bool show_connections{true};
    int  connections_limit{30};     ///< Max connections to display
    bool connections_show_listen{false}; ///< Show LISTEN sockets

    // Processes
    bool show_processes{true};
    int  proc_limit{20};            ///< Max processes to show
    bool show_proc_threads{true};
    bool show_proc_network{false};  ///< Per-process rx/tx (expensive)

    // ------------------------------------------------------------------
    // [tui]
    // ------------------------------------------------------------------
    bool tui_use_unicode{true};     ///< Unicode block chars for bars/sparklines
    bool tui_use_colors{true};      ///< ANSI 24-bit color
    int  sparkline_length{40};      ///< Number of history samples for sparklines
    int  proc_sort_col{0};          ///< 0=CPU, 1=MEM, 2=PID

    // ------------------------------------------------------------------
    // Methods
    // ------------------------------------------------------------------

    /** @brief Return the default config file path (XDG/HOME based). */
    static std::string default_config_path();

    /** @brief Load config from disk; returns defaults if file missing. */
    static Config load();

    /** @brief Load config from a specific path. */
    static Config load_from(const std::string& path);

    /** @brief Save config to disk (creates directory if needed). */
    void save() const;

    /** @brief Save config to a specific path. */
    void save_to(const std::string& path) const;

    /** @brief Return a Config with all factory defaults. */
    static Config defaults();

    /** @brief Human-readable dump of all settings. */
    std::string to_string() const;

    /** @brief Build DisplayFlags from current config settings. */
    struct DisplayFlags to_display_flags() const;
};

#endif // SYSMON_CONFIG_HPP
