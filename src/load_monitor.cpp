#include "sysmon/load_monitor.hpp"
#include "sysmon/platform.hpp"
#include "sysmon/utils.hpp"

#include <sstream>
#include <vector>

#if defined(SYSMON_MACOS)
#  include <sys/sysctl.h>
#  include <sys/proc.h>
#  include <sys/proc_info.h>
#  include <libproc.h>
#endif

#if defined(SYSMON_LINUX)
#  include <filesystem>
#endif

#if defined(SYSMON_WINDOWS)
#  include <windows.h>
#  include <tlhelp32.h>
#endif

LoadStats LoadMonitor::read() {
    LoadStats stats;

#if defined(SYSMON_LINUX)
    if (auto loadavg = utils::read_file("/proc/loadavg")) {
        // Format: "1.00 0.50 0.25 1/200 12345"
        const auto parts = utils::split_whitespace(loadavg.value());
        if (parts.size() >= 3) {
            stats.load_1min  = utils::to_double(parts[0]).value_or(0.0);
            stats.load_5min  = utils::to_double(parts[1]).value_or(0.0);
            stats.load_15min = utils::to_double(parts[2]).value_or(0.0);
        }
        if (parts.size() >= 4) {
            const auto slash = parts[3].find('/');
            if (slash != std::string::npos) {
                stats.running_processes = static_cast<unsigned int>(
                    utils::to_int(parts[3].substr(0, slash)).value_or(0));
                stats.total_processes = static_cast<unsigned int>(
                    utils::to_int(parts[3].substr(slash + 1)).value_or(0));
            }
        }
    }

    // Per-state counts and the thread total come from walking /proc.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        unsigned int sleeping = 0, stopped = 0, zombie = 0, running = 0, threads = 0, procs = 0;

        for (const auto& entry : fs::directory_iterator("/proc", ec)) {
            if (ec) break;
            const std::string name = entry.path().filename().string();
            if (name.empty() || !std::isdigit(static_cast<unsigned char>(name[0]))) continue;

            auto status = utils::read_file(entry.path().string() + "/status");
            if (!status.has_value()) continue;
            ++procs;

            std::istringstream iss(status.value());
            std::string line;
            while (std::getline(iss, line)) {
                if (utils::starts_with(line, "State:")) {
                    const auto fields = utils::split_whitespace(line);
                    if (fields.size() >= 2) {
                        switch (fields[1][0]) {
                            case 'R': ++running;  break;
                            case 'S': case 'D': ++sleeping; break;
                            case 'T': case 't': ++stopped;  break;
                            case 'Z': ++zombie;   break;
                            default: break;
                        }
                    }
                } else if (utils::starts_with(line, "Threads:")) {
                    const auto fields = utils::split_whitespace(line);
                    if (fields.size() >= 2) {
                        threads += static_cast<unsigned int>(utils::to_int(fields[1]).value_or(0));
                    }
                }
            }
        }

        stats.sleeping_processes = sleeping;
        stats.stopped_processes  = stopped;
        stats.zombie_processes   = zombie;
        stats.total_threads      = threads;
        if (stats.total_processes == 0) stats.total_processes = procs;
        if (stats.running_processes == 0) stats.running_processes = running;
    }

#elif defined(SYSMON_MACOS)
    double load[3] = {0, 0, 0};
    if (getloadavg(load, 3) == 3) {
        stats.load_1min  = load[0];
        stats.load_5min  = load[1];
        stats.load_15min = load[2];
    }

    // Walk the kinfo_proc array so the per-state counts are real numbers and
    // not, as before, a total with a hard-coded zero for "running".
    {
        int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
        size_t size = 0;
        if (sysctl(mib, 4, nullptr, &size, nullptr, 0) == 0 && size > 0) {
            // The table can grow between sizing and reading, so ask for headroom.
            size += size / 8;
            std::vector<char> buffer(size);
            if (sysctl(mib, 4, buffer.data(), &size, nullptr, 0) == 0) {
                const size_t count = size / sizeof(struct kinfo_proc);
                const auto* procs = reinterpret_cast<const struct kinfo_proc*>(buffer.data());
                stats.total_processes = static_cast<unsigned int>(count);

                // kinfo_proc reports p_stat as SRUN for essentially every
                // process on macOS, because run state lives on the thread, not
                // the process.  Ask the task layer how many threads are really
                // running instead of copying out a field that is always "run".
                for (size_t i = 0; i < count; ++i) {
                    const int pid = procs[i].kp_proc.p_pid;

                    if (procs[i].kp_proc.p_stat == SZOMB) {
                        ++stats.zombie_processes;
                        continue;
                    }
                    if (procs[i].kp_proc.p_stat == SSTOP) {
                        ++stats.stopped_processes;
                        continue;
                    }

                    struct proc_taskinfo pti{};
                    if (proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &pti, sizeof(pti)) == sizeof(pti)) {
                        stats.total_threads += static_cast<unsigned int>(pti.pti_threadnum);
                        if (pti.pti_numrunning > 0) ++stats.running_processes;
                        else                        ++stats.sleeping_processes;
                    } else {
                        // Kernel-only or vanished process: not observable, so
                        // count it as sleeping rather than as running.
                        ++stats.sleeping_processes;
                    }
                }
            }
        }
    }

#elif defined(SYSMON_WINDOWS)
    // Windows exposes no load average.  Report the process/thread census
    // instead of synthesising a load figure that would not mean anything.
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(snapshot, &entry)) {
                do {
                    ++stats.total_processes;
                    stats.total_threads += entry.cntThreads;
                } while (Process32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
        }
    }
#endif

    return stats;
}
