/**
 * @file stats.hpp
 * @brief All data structures for sysmon.
 *
 * Plain, dependency-free POD-like structs used to pass data between the
 * monitor layer and the rendering layer.  All byte counts are in bytes;
 * percentages are in the range [0, 100].
 */

#ifndef SYSMON_STATS_HPP
#define SYSMON_STATS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// ===========================================================================
// System
// ===========================================================================

/** @brief General system metadata. */
struct SystemStats {
    std::string hostname;
    std::string os;
    std::string kernel;
    std::string architecture;
    std::string uptime;
    double      uptime_seconds{0.0};
};

// ===========================================================================
// CPU
// ===========================================================================

/** @brief Per-logical-core snapshot. */
struct CoreStats {
    unsigned int id{0};
    double       usage_percent{0.0};
    double       user_percent{0.0};
    double       system_percent{0.0};
    double       iowait_percent{0.0};
    double       idle_percent{0.0};
    double       frequency_mhz{0.0};
    std::optional<double> temperature_celsius;
};

/** @brief Aggregate CPU statistics. */
struct CpuStats {
    std::string  model;
    unsigned int logical_cores{0};
    unsigned int physical_cores{0};
    double       usage_percent{0.0};
    double       user_percent{0.0};
    double       system_percent{0.0};
    double       iowait_percent{0.0};
    double       idle_percent{0.0};
    double       frequency_mhz{0.0};
    double       max_frequency_mhz{0.0};
    std::optional<double> temperature_celsius;
    std::vector<CoreStats> per_core;
};

// ===========================================================================
// Memory
// ===========================================================================

/** @brief RAM and swap statistics. */
struct MemoryStats {
    uint64_t ram_total_bytes{0};
    uint64_t ram_used_bytes{0};
    uint64_t ram_available_bytes{0};
    uint64_t ram_cached_bytes{0};
    uint64_t ram_buffer_bytes{0};
    uint64_t swap_total_bytes{0};
    uint64_t swap_used_bytes{0};
    double   ram_usage_percent{0.0};
    double   swap_usage_percent{0.0};
};

// ===========================================================================
// GPU
// ===========================================================================

/** @brief Single GPU / integrated-graphics statistics. */
struct GpuStats {
    std::string  name;                       ///< e.g. "Apple GPU", "RTX 4090"
    std::string  vendor;                     ///< "Apple", "NVIDIA", "AMD", "Intel"
    unsigned int gpu_cores{0};              ///< Shader / compute cores
    std::string  memory_type;               ///< "Unified", "GDDR6X", "HBM2e", …
    uint64_t     memory_total_bytes{0};
    uint64_t     memory_used_bytes{0};
    uint64_t     memory_free_bytes{0};
    double       usage_percent{0.0};        ///< Overall GPU utilization [0..100]
    double       memory_usage_percent{0.0};
    double       frequency_mhz{0.0};        ///< Core clock
    double       memory_frequency_mhz{0.0}; ///< Memory clock
    double       encoder_percent{0.0};
    double       decoder_percent{0.0};
    std::optional<double> temperature_celsius;
    std::optional<double> power_watts;
};

// ===========================================================================
// Load
// ===========================================================================

/** @brief System load averages. */
struct LoadStats {
    double       load_1min{0.0};
    double       load_5min{0.0};
    double       load_15min{0.0};
    unsigned int running_processes{0};
    unsigned int total_processes{0};
};

// ===========================================================================
// Disk
// ===========================================================================

/** @brief Filesystem space usage. */
struct DiskStats {
    std::string mountpoint;
    std::string device;
    std::string filesystem_type;
    uint64_t    total_bytes{0};
    uint64_t    used_bytes{0};
    uint64_t    available_bytes{0};
    double      usage_percent{0.0};
};

/** @brief Disk I/O throughput per block device. */
struct DiskIOStats {
    std::string  device;
    double       read_bytes_per_sec{0.0};
    double       write_bytes_per_sec{0.0};
    double       read_ops_per_sec{0.0};
    double       write_ops_per_sec{0.0};
    double       util_percent{0.0};         ///< Device busy % (Linux only)
};

// ===========================================================================
// Network
// ===========================================================================

/** @brief Per-interface network statistics. */
struct NetworkStats {
    std::string  interface;
    uint64_t     rx_bytes_total{0};
    uint64_t     tx_bytes_total{0};
    uint64_t     rx_packets_total{0};
    uint64_t     tx_packets_total{0};
    uint64_t     rx_errors{0};
    uint64_t     tx_errors{0};
    double       rx_bytes_per_sec{0.0};
    double       tx_bytes_per_sec{0.0};
    std::string  ip_address;
    std::string  ip6_address;
    bool         is_up{false};
    uint64_t     speed_mbps{0};
};

/** @brief A single TCP/UDP connection. */
struct NetConnectionStats {
    std::string  local_addr;
    uint16_t     local_port{0};
    std::string  remote_addr;
    uint16_t     remote_port{0};
    std::string  state;           ///< "ESTABLISHED", "LISTEN", "TIME_WAIT", …
    std::string  protocol;        ///< "TCP", "TCP6", "UDP", "UDP6"
    int          pid{-1};
    std::string  process_name;
    uint64_t     rx_bytes{0};     ///< Best-effort (Linux only)
    uint64_t     tx_bytes{0};
};

// ===========================================================================
// Processes
// ===========================================================================

/** @brief Single process snapshot. */
struct ProcessStats {
    int          pid{0};
    std::string  name;
    std::string  user;
    std::string  state;           ///< R, S, D, Z, T, …
    double       cpu_percent{0.0};
    uint64_t     mem_rss_bytes{0};
    uint64_t     mem_vms_bytes{0};
    double       mem_percent{0.0};
    unsigned int threads{0};
    long long    start_time{0};
    double       rx_bytes_per_sec{0.0};  ///< Network rx (best-effort)
    double       tx_bytes_per_sec{0.0};  ///< Network tx (best-effort)
};

// ===========================================================================
// Temperature / Sensors
// ===========================================================================

/** @brief A single temperature sensor reading. */
struct SensorReading {
    std::string  name;
    std::string  chip;                       ///< Chip/driver name (e.g. "coretemp")
    double       temperature_celsius{0.0};
    std::optional<double> high;
    std::optional<double> critical;
    bool         enabled{true};             ///< Can be toggled via config
};

/** @brief All temperature sensor readings. */
struct TemperatureStats {
    std::vector<SensorReading> sensors;
    std::optional<double>      cpu_package;
};

// ===========================================================================
// Configuration snapshot (passed to renderers)
// ===========================================================================

/** @brief Which sections to display — derived from Config at render time. */
struct DisplayFlags {
    bool cpu{true};
    bool cpu_per_core{true};
    bool cpu_cores_detail{true};
    bool memory{true};
    bool swap{true};
    bool gpu{true};
    bool gpu_memory{true};
    bool temperature{true};
    bool temperature_per_sensor{true};
    bool disk{true};
    bool disk_io{true};
    bool network{true};
    bool network_per_iface{true};
    bool connections{true};
    bool processes{true};
    bool compact{false};
    int  proc_limit{20};
};

#endif // SYSMON_STATS_HPP