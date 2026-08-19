/**
 * @file cpu_monitor.hpp
 * @brief CPU usage, frequency, temperature and per-core statistics.
 */

#ifndef SYSMON_CPU_MONITOR_HPP
#define SYSMON_CPU_MONITOR_HPP

#include "sysmon/stats.hpp"
#include <optional>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <cstdint>

/**
 * @brief Collects CPU statistics.
 *
 * On Linux reads /proc/stat and /proc/cpuinfo.
 * On macOS uses sysctl and host_processor_info().
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

private:
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

    std::vector<CpuTimes>      prev_times_;  ///< Previous per-core times
    CpuTimes                   prev_total_;  ///< Previous aggregate times
    std::chrono::steady_clock::time_point prev_ts_;
    bool                       first_read_{true};

    // Linux helpers
    std::vector<CpuTimes> read_proc_stat_linux();

    // macOS helpers
    std::vector<CpuTimes> read_cpu_times_macos();

    // Common helpers
    std::optional<std::string>  get_cpu_model();
    std::optional<unsigned int> get_logical_cores();
    std::optional<unsigned int> get_physical_cores();
    std::optional<double>       get_cpu_frequency();
    std::optional<double>       get_max_frequency();

    double usage_from_delta(const CpuTimes& a, const CpuTimes& b, double* user_pct = nullptr,
                             double* sys_pct = nullptr, double* iowait_pct = nullptr,
                             double* idle_pct = nullptr);
};

#endif // SYSMON_CPU_MONITOR_HPP