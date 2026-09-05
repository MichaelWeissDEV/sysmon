/**
 * @file stats.hpp
 * @brief All data structures for sysmon.
 *
 * Plain, dependency-free POD-like structs used to pass data between the
 * monitor layer and the rendering layer.  All byte counts are in bytes;
 * percentages are in the range [0, 100].
 *
 * @par Optionality contract
 * A value that a platform cannot measure through a stable, unprivileged API is
 * represented as an empty std::optional and rendered as `N/A`.  sysmon never
 * substitutes a guess, an estimate or a zero for a metric it did not read.
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
    std::string os;                ///< Pretty OS name, e.g. "Ubuntu 24.04 LTS"
    std::string os_build;          ///< Build / release identifier, if any
    std::string kernel;            ///< Kernel or NT version
    std::string architecture;      ///< e.g. "x86_64", "arm64"
    std::string machine_model;     ///< Hardware model, e.g. "Mac16,5"
    std::string uptime;            ///< Human formatted uptime
    double      uptime_seconds{0.0};
    std::string boot_time;         ///< Local time the machine booted
    std::string current_time;      ///< Local wall-clock time of the sample
    std::string timezone;          ///< e.g. "CEST"
    std::string virtualization;    ///< Hypervisor / container runtime, if detected
    std::optional<unsigned int> logged_in_users;
    std::optional<unsigned int> process_count;
    std::optional<unsigned int> thread_count;
    std::optional<uint64_t>     page_size_bytes;
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
    double       nice_percent{0.0};
    double       irq_percent{0.0};
    double       steal_percent{0.0};
    std::optional<double> frequency_mhz;      ///< nullopt if not measurable
    std::optional<double> temperature_celsius;
    std::string  cluster;                     ///< "P"/"E" on hybrid CPUs, else empty
};

/** @brief Aggregate CPU statistics. */
struct CpuStats {
    std::string  model;
    std::string  vendor;
    unsigned int logical_cores{0};
    unsigned int physical_cores{0};
    std::optional<unsigned int> sockets;
    std::optional<unsigned int> threads_per_core;
    std::optional<unsigned int> performance_cores;  ///< Hybrid CPUs only
    std::optional<unsigned int> efficiency_cores;   ///< Hybrid CPUs only
    double       usage_percent{0.0};
    double       user_percent{0.0};
    double       system_percent{0.0};
    double       iowait_percent{0.0};
    double       idle_percent{0.0};
    double       nice_percent{0.0};
    double       irq_percent{0.0};
    double       steal_percent{0.0};
    std::optional<double> frequency_mhz;      ///< nullopt if not measurable
    std::optional<double> min_frequency_mhz;
    std::optional<double> max_frequency_mhz;  ///< nullopt if not measurable
    std::optional<double> base_frequency_mhz;
    std::optional<double> temperature_celsius;
    std::string  thermal_pressure;            ///< macOS: Nominal/Fair/Serious/Critical
    std::optional<uint64_t> cache_l1d_bytes;
    std::optional<uint64_t> cache_l1i_bytes;
    std::optional<uint64_t> cache_l2_bytes;
    std::optional<uint64_t> cache_l3_bytes;
    std::optional<double> context_switches_per_sec;
    std::optional<double> interrupts_per_sec;
    std::optional<double> forks_per_sec;
    std::optional<uint64_t> total_context_switches;
    std::optional<uint64_t> total_interrupts;
    std::vector<std::string> flags;           ///< Instruction-set features
    std::vector<CoreStats>   per_core;
};

// ===========================================================================
// Memory
// ===========================================================================

/** @brief RAM and swap statistics. */
struct MemoryStats {
    uint64_t ram_total_bytes{0};
    uint64_t ram_used_bytes{0};
    uint64_t ram_available_bytes{0};
    uint64_t ram_free_bytes{0};
    uint64_t ram_cached_bytes{0};
    uint64_t ram_buffer_bytes{0};
    uint64_t swap_total_bytes{0};
    uint64_t swap_used_bytes{0};
    double   ram_usage_percent{0.0};
    double   swap_usage_percent{0.0};

    // Detailed breakdown (platform dependent; nullopt when unavailable)
    std::optional<uint64_t> active_bytes;
    std::optional<uint64_t> inactive_bytes;
    std::optional<uint64_t> wired_bytes;      ///< macOS wired / Linux unevictable
    std::optional<uint64_t> compressed_bytes; ///< macOS compressor / Linux zswap
    std::optional<uint64_t> shared_bytes;
    std::optional<uint64_t> slab_bytes;
    std::optional<uint64_t> dirty_bytes;
    std::optional<uint64_t> commit_total_bytes;  ///< Windows commit charge
    std::optional<uint64_t> commit_limit_bytes;
    std::optional<double>   page_faults_per_sec;
    std::optional<double>   major_faults_per_sec;
    std::optional<double>   page_ins_per_sec;
    std::optional<double>   page_outs_per_sec;
    std::optional<double>   swap_ins_per_sec;
    std::optional<double>   swap_outs_per_sec;
    std::optional<double>   pressure_percent; ///< Memory pressure, where exposed
};

// ===========================================================================
// GPU
// ===========================================================================

/** @brief Single GPU / integrated-graphics statistics. */
struct GpuStats {
    std::string  name;                       ///< e.g. "Apple GPU", "RTX 4090"
    std::string  vendor;                     ///< "Apple", "NVIDIA", "AMD", "Intel"
    std::string  driver_version;
    std::optional<unsigned int> gpu_cores;   ///< Shader / compute cores
    std::string  memory_type;                ///< "Unified", "GDDR6X", "HBM2e", …
    std::optional<uint64_t> memory_total_bytes;
    std::optional<uint64_t> memory_used_bytes;
    std::optional<uint64_t> memory_free_bytes;
    std::optional<double> usage_percent;        ///< Overall GPU utilization [0..100]
    std::optional<double> memory_usage_percent;
    std::optional<double> frequency_mhz;        ///< Core clock
    std::optional<double> memory_frequency_mhz; ///< Memory clock
    std::optional<double> encoder_percent;
    std::optional<double> decoder_percent;
    std::optional<double> temperature_celsius;
    std::optional<double> power_watts;
    std::optional<double> fan_percent;
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
    unsigned int sleeping_processes{0};
    unsigned int stopped_processes{0};
    unsigned int zombie_processes{0};
    unsigned int total_threads{0};
    std::optional<double> load_per_core_1min;  ///< load_1min / logical cores
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
    uint64_t    available_bytes{0};   ///< Free space usable by an unprivileged user
    uint64_t    free_bytes{0};        ///< Total free space including reserve
    double      usage_percent{0.0};
    bool        read_only{false};
    bool        removable{false};
    std::optional<uint64_t> inodes_total;
    std::optional<uint64_t> inodes_used;
    std::optional<uint64_t> inodes_free;
    std::optional<double>   inode_usage_percent;
    std::optional<uint64_t> block_size;
    std::string mount_options;
};

/** @brief Disk I/O throughput per block device. */
struct DiskIOStats {
    std::string  device;
    double       read_bytes_per_sec{0.0};
    double       write_bytes_per_sec{0.0};
    double       read_ops_per_sec{0.0};
    double       write_ops_per_sec{0.0};
    uint64_t     read_bytes_total{0};
    uint64_t     write_bytes_total{0};
    uint64_t     read_ops_total{0};
    uint64_t     write_ops_total{0};
    std::optional<double> util_percent;      ///< Device busy %
    std::optional<double> avg_read_latency_ms;
    std::optional<double> avg_write_latency_ms;
    std::optional<double> queue_depth;
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
    uint64_t     rx_dropped{0};
    uint64_t     tx_dropped{0};
    double       rx_bytes_per_sec{0.0};
    double       tx_bytes_per_sec{0.0};
    double       rx_packets_per_sec{0.0};
    double       tx_packets_per_sec{0.0};
    std::string  ip_address;
    std::string  ip6_address;
    std::string  netmask;
    std::string  mac_address;
    bool         is_up{false};
    bool         is_loopback{false};
    bool         is_wireless{false};
    std::optional<uint32_t> mtu;
    std::optional<uint64_t> speed_mbps;   ///< nullopt if not measurable
    std::string  duplex;                  ///< "full", "half" or empty
};

/** @brief Host-wide networking facts that are not per interface. */
struct NetGlobalStats {
    std::string              default_gateway_v4;
    std::string              default_gateway_v6;
    std::vector<std::string> dns_servers;
    std::string              domain;
    unsigned int             tcp_established{0};
    unsigned int             tcp_listen{0};
    unsigned int             tcp_time_wait{0};
    unsigned int             tcp_other{0};
    unsigned int             udp_sockets{0};
    unsigned int             total_connections{0};
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
    int          ppid{0};
    std::string  name;
    std::string  cmdline;
    std::string  user;
    std::string  state;           ///< R, S, D, Z, T, …
    double       cpu_percent{0.0};
    uint64_t     mem_rss_bytes{0};
    uint64_t     mem_vms_bytes{0};
    double       mem_percent{0.0};
    unsigned int threads{0};
    long long    start_time{0};        ///< Unix epoch seconds, 0 when unknown
    double       cpu_time_seconds{0.0};///< Accumulated user+system CPU time
    std::optional<int>      nice;
    std::optional<uint64_t> open_files;
    std::optional<double>   io_read_bytes_per_sec;
    std::optional<double>   io_write_bytes_per_sec;
    double       rx_bytes_per_sec{0.0};  ///< Network rx (best-effort)
    double       tx_bytes_per_sec{0.0};
};

/** @brief Sort order for the process table. */
enum class ProcSort {
    Cpu,      ///< Descending CPU%
    Memory,   ///< Descending RSS
    Pid,      ///< Ascending PID
    Name,     ///< Ascending name
    Time      ///< Descending accumulated CPU time
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

/** @brief A single fan reading. */
struct FanReading {
    std::string  name;
    std::string  chip;
    double       rpm{0.0};
    std::optional<double> min_rpm;
    std::optional<double> max_rpm;
};

/** @brief All temperature and fan sensor readings. */
struct TemperatureStats {
    std::vector<SensorReading> sensors;
    std::vector<FanReading>    fans;
    std::optional<double>      cpu_package;
    std::optional<double>      hottest_celsius;
    std::string                hottest_name;
};

// ===========================================================================
// Power / Battery
// ===========================================================================

/** @brief Battery and power-source state. */
struct BatteryStats {
    bool        present{false};
    bool        ac_connected{false};
    std::string state;                        ///< "Charging", "Discharging", "Full", …
    std::string technology;
    std::string vendor;
    std::optional<double>   percent;
    std::optional<double>   time_remaining_minutes;
    std::optional<unsigned int> cycle_count;
    std::optional<double>   health_percent;   ///< Current / design capacity
    std::optional<double>   temperature_celsius;
    std::optional<double>   voltage_volts;
    std::optional<double>   power_watts;      ///< Negative = discharging
    std::optional<uint64_t> design_capacity_mah;
    std::optional<uint64_t> full_capacity_mah;
    std::optional<uint64_t> current_capacity_mah;
};

// ===========================================================================
// Aggregate snapshot
// ===========================================================================

/** @brief One complete sample of every subsystem, as handed to a renderer. */
struct Snapshot {
    SystemStats                       system;
    CpuStats                          cpu;
    MemoryStats                       memory;
    std::vector<GpuStats>             gpus;
    LoadStats                         load;
    BatteryStats                      battery;
    TemperatureStats                  temperatures;
    std::vector<DiskStats>            disks;
    std::vector<DiskIOStats>          disk_io;
    std::vector<NetworkStats>         network;
    NetGlobalStats                    net_global;
    std::vector<NetConnectionStats>   connections;
    std::vector<ProcessStats>         processes;
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
    bool battery{true};
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
