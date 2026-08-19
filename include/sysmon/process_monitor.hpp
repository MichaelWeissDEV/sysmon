/**
 * @file process_monitor.hpp
 * @brief Process list monitor.
 */

#ifndef SYSMON_PROCESS_MONITOR_HPP
#define SYSMON_PROCESS_MONITOR_HPP

#include "sysmon/stats.hpp"
#include "sysmon/platform.hpp"
#include <vector>
#include <string>
#include <map>
#include <chrono>

/**
 * @brief Collects process information and computes per-process CPU usage.
 *
 * On Linux this reads /proc/<pid>/stat and /proc/<pid>/status.
 * On macOS it uses sysctl / proc_pidinfo.
 */
class ProcessMonitor {
public:
    ProcessMonitor();

    /**
     * @brief Read the current process list.
     * @param limit  Maximum number of processes to return (sorted by CPU).
     *               0 = return all.
     * @return Vector of ProcessStats sorted by cpu_percent descending.
     */
    std::vector<ProcessStats> read(unsigned int limit = 20);

private:
    struct ProcSnapshot {
        unsigned long long utime{0};
        unsigned long long stime{0};
        std::chrono::steady_clock::time_point timestamp;
    };

    std::map<int, ProcSnapshot> previous_snapshots_;
#if defined(SYSMON_LINUX)
    unsigned long long          total_cpu_time_prev_{0};
#endif

    // Platform-specific
    std::vector<ProcessStats> read_linux(unsigned int limit);
    std::vector<ProcessStats> read_macos(unsigned int limit);

    // Helpers
    std::string get_username(unsigned int uid);
    std::string read_proc_name(int pid);
};

#endif // SYSMON_PROCESS_MONITOR_HPP
