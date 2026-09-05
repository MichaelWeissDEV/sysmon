/**
 * @file cpu_monitor.hpp
 * @brief CPU usage, frequency, temperature and per-core statistics.
 */

#ifndef SYSMON_CPU_MONITOR_HPP
#define SYSMON_CPU_MONITOR_HPP

#include "sysmon/stats.hpp"
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Collects CPU statistics.
 *
 * - Linux:   /proc/stat, /proc/cpuinfo, /sys/devices/system/cpu
 * - macOS:   sysctl and host_processor_info()
 * - Windows: NtQuerySystemInformation, GetLogicalProcessorInformationEx, registry
 */
class CpuMonitor {
public:
    CpuMonitor();

    /**
     * @brief Read current CPU statistics.
     *
     * The first call returns zero usage (no previous sample).
     * Subsequent calls compute the delta since the last call.
     */
    CpuStats read();

    /** @brief Per-CPU time counter snapshot. */
    struct CpuTimes {
        unsigned long long user{0};
        unsigned long long nice{0};
        unsigned long long system{0};
        unsigned long long idle{0};
        unsigned long long iowait{0};
        unsigned long long irq{0};
        unsigned long long softirq{0};
        unsigned long long steal{0};
    };

    /** @brief Percentage split of one CPU-time delta across all states. */
    struct Breakdown {
        double user{0.0};
        double system{0.0};
        double iowait{0.0};
        double idle{0.0};
        double nice{0.0};
        double irq{0.0};
        double steal{0.0};
    };

    /**
     * @brief Compute usage percentage from two successive time snapshots.
     *
     * Always returns a value clamped to [0, 100].  Counter resets or wraps
     * yield 0.0 rather than a bogus value.
     */
    static double usage_from_delta(const CpuTimes& a, const CpuTimes& b,
                                   double* user_pct = nullptr,
                                   double* sys_pct = nullptr,
                                   double* iowait_pct = nullptr,
                                   double* idle_pct = nullptr);

    /** @brief usage_from_delta() variant that fills a full state breakdown. */
    static double usage_from_delta(const CpuTimes& a, const CpuTimes& b, Breakdown& out);

    /** @brief Counters from the summary lines of /proc/stat. */
    struct KernelCounters {
        std::optional<uint64_t> context_switches;
        std::optional<uint64_t> interrupts;
        std::optional<uint64_t> forks;
        std::optional<uint64_t> procs_running;
        std::optional<uint64_t> procs_blocked;
    };

    /** @brief Parse the ctxt/intr/processes lines out of /proc/stat content. */
    static KernelCounters parse_proc_stat_counters(const std::string& content);

private:
    std::vector<CpuTimes>      prev_times_;  ///< Previous per-core times
    CpuTimes                   prev_total_;  ///< Previous aggregate times
    std::chrono::steady_clock::time_point prev_ts_;
    bool                       first_read_{true};
    KernelCounters             prev_counters_{};

    // Linux helpers
    std::vector<CpuTimes> read_proc_stat_linux();

    // macOS helpers
    std::vector<CpuTimes> read_cpu_times_macos();

    // Windows helpers
    std::vector<CpuTimes> read_cpu_times_windows();

    // Common helpers
    std::optional<std::string>  get_cpu_model();
    std::optional<unsigned int> get_logical_cores();
    std::optional<unsigned int> get_physical_cores();
    std::optional<double>       get_cpu_frequency();
    std::optional<double>       get_max_frequency();

    /// Fill in cache sizes, vendor, sockets, flags and other static facts.
    void fill_static_info(CpuStats& stats);

    /// Fill in ctxt/intr/fork rates from the kernel counters.
    void fill_kernel_rates(CpuStats& stats, double elapsed_seconds);
};

#endif // SYSMON_CPU_MONITOR_HPP
