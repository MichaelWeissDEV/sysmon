/**
 * @file process_monitor.hpp
 * @brief Process list monitor.
 */

#ifndef SYSMON_PROCESS_MONITOR_HPP
#define SYSMON_PROCESS_MONITOR_HPP

#include "sysmon/platform.hpp"
#include "sysmon/stats.hpp"
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

/**
 * @brief Collects process information and computes per-process CPU usage.
 *
 * - Linux:   /proc/<pid>/{stat,status,io,cmdline,fd}
 * - macOS:   sysctl KERN_PROC_ALL and proc_pidinfo / proc_pid_rusage
 * - Windows: Toolhelp32 plus the per-process Win32 query APIs
 *
 * CPU percentages use the convention that 100 % is one fully busy logical core,
 * so a process on an 8-core machine can legitimately report up to 800 %.
 */
class ProcessMonitor {
public:
    ProcessMonitor();

    /**
     * @brief Read the current process list.
     * @param limit  Maximum number of processes to return (0 = all).
     * @param sort   Ordering applied before the limit is enforced.
     * @return Vector of ProcessStats in the requested order.
     */
    std::vector<ProcessStats> read(unsigned int limit = 20,
                                   ProcSort sort = ProcSort::Cpu);

    /** @brief Order a process list in place. Exposed for testing. */
    static void sort_processes(std::vector<ProcessStats>& procs, ProcSort sort);

    /**
     * @brief List the files, sockets and pipes one process has open.
     *
     * Deliberately per-process and on demand.  Walking every process's
     * descriptor table would mean thousands of readlink() calls per refresh —
     * far more expensive than the rest of sysmon put together — and it is only
     * ever one process a user wants to look inside.
     *
     * The kernel refuses another user's process to an unprivileged caller.
     * That comes back as OpenFilesStatus::PermissionDenied rather than as an
     * empty list, because "this process has no files open" and "you may not
     * look" are different answers.
     *
     * @param pid Process to inspect.
     */
    static OpenFilesResult open_files(int pid);

private:
    struct ProcSnapshot {
        unsigned long long utime{0};
        unsigned long long stime{0};
        uint64_t io_read_bytes{0};
        uint64_t io_write_bytes{0};
        std::chrono::steady_clock::time_point timestamp;
    };

    std::map<int, ProcSnapshot> previous_snapshots_;
#if defined(SYSMON_LINUX)
    unsigned long long          total_cpu_time_prev_{0};
#endif

    // Platform-specific
    std::vector<ProcessStats> read_linux(unsigned int limit);
    std::vector<ProcessStats> read_macos(unsigned int limit);
    std::vector<ProcessStats> read_windows(unsigned int limit);

    // Helpers
    std::string get_username(unsigned int uid);

    /// Forget entries for processes that no longer exist, so the snapshot map
    /// does not grow without bound on a busy machine.
    void prune_snapshots(const std::vector<ProcessStats>& live);
};

#endif // SYSMON_PROCESS_MONITOR_HPP
