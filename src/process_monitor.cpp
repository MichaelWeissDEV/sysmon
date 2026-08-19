#include "sysmon/process_monitor.hpp"
#include "sysmon/utils.hpp"
#include "sysmon/platform.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <pwd.h>
#include <unistd.h>
#include <climits>

#if defined(SYSMON_MACOS)
#  include <sys/sysctl.h>
#  include <libproc.h>
#  include <mach/mach.h>
#  include <mach/mach_time.h>
#endif

ProcessMonitor::ProcessMonitor() = default;

std::vector<ProcessStats> ProcessMonitor::read(unsigned int limit) {
#if defined(SYSMON_LINUX)
    return read_linux(limit);
#elif defined(SYSMON_MACOS)
    return read_macos(limit);
#else
    (void)limit;
    return {};
#endif
}

// ---------------------------------------------------------------------------
// Linux
// ---------------------------------------------------------------------------

#if defined(SYSMON_LINUX)

std::vector<ProcessStats> ProcessMonitor::read_linux(unsigned int limit) {
    namespace fs = std::filesystem;

    auto now    = std::chrono::steady_clock::now();
    long hz     = sysconf(_SC_CLK_TCK);
    long page_s = sysconf(_SC_PAGE_SIZE);

    // Read total CPU time from /proc/stat for % calculation
    unsigned long long total_cpu = 0;
    auto stat_file = utils::read_file("/proc/stat");
    if (stat_file.has_value()) {
        std::istringstream iss(stat_file.value());
        std::string line;
        std::getline(iss, line); // "cpu ..."
        auto parts = utils::split(line, ' ');
        for (size_t i = 1; i < parts.size(); ++i) {
            try { total_cpu += std::stoull(parts[i]); } catch (...) {}
        }
    }

    double cpu_delta = static_cast<double>(total_cpu) -
                       static_cast<double>(total_cpu_time_prev_);
    total_cpu_time_prev_ = total_cpu;

    std::vector<ProcessStats> result;

    try {
        for (const auto& entry : fs::directory_iterator("/proc")) {
            if (!entry.is_directory()) continue;

            std::string fname = entry.path().filename().string();
            if (fname.empty() || !std::isdigit(fname[0])) continue;

            int pid = 0;
            try { pid = std::stoi(fname); } catch (...) { continue; }

            std::string proc_dir = "/proc/" + fname;
            ProcessStats ps;
            ps.pid = pid;

            auto stat = utils::read_file(proc_dir + "/stat");
            if (!stat.has_value()) continue;

            auto& s = stat.value();
            size_t comm_start = s.find('(');
            size_t comm_end   = s.rfind(')');
            if (comm_start == std::string::npos || comm_end == std::string::npos) continue;

            ps.name = s.substr(comm_start + 1, comm_end - comm_start - 1);

            std::string rest = s.substr(comm_end + 2);
            auto fields = utils::split(rest, ' ');
            if (fields.size() < 20) continue;

            ps.state = fields[0];
            unsigned long long utime = 0, stime = 0;
            try {
                utime = std::stoull(fields[11]);
                stime = std::stoull(fields[12]);
            } catch (...) { continue; }

            // CPU usage calculation
            auto it = previous_snapshots_.find(pid);
            if (it != previous_snapshots_.end() && cpu_delta > 0) {
                double proc_delta = static_cast<double>(utime + stime) -
                                    static_cast<double>(it->second.utime + it->second.stime);
                ps.cpu_percent = (proc_delta / cpu_delta) * 100.0;
                if (ps.cpu_percent < 0) ps.cpu_percent = 0;
            }

            ProcSnapshot snap;
            snap.utime     = utime;
            snap.stime     = stime;
            snap.timestamp = now;
            previous_snapshots_[pid] = snap;

            // Status for uid, threads, RSS
            auto status = utils::read_file(proc_dir + "/status");
            if (status.has_value()) {
                std::istringstream ss(status.value());
                std::string line;
                while (std::getline(ss, line)) {
                    if (line.substr(0, 4) == "Uid:") {
                        auto parts = utils::split(line.substr(4), '\t');
                        if (!parts.empty()) {
                            try {
                                unsigned int uid = static_cast<unsigned int>(std::stoul(parts[0]));
                                ps.user = get_username(uid);
                            } catch (...) {}
                        }
                    } else if (line.substr(0, 8) == "Threads:") {
                        try {
                            ps.threads = static_cast<unsigned int>(
                                std::stoul(utils::trim(line.substr(8))));
                        } catch (...) {}
                    } else if (line.substr(0, 6) == "VmRSS:") {
                        try {
                            auto parts = utils::split(line.substr(6), ' ');
                            for (auto& p : parts) {
                                if (!p.empty() && std::isdigit(p[0])) {
                                    ps.mem_rss_bytes = std::stoull(p) * 1024;
                                    break;
                                }
                            }
                        } catch (...) {}
                    } else if (line.substr(0, 7) == "VmSize:") {
                        try {
                            auto parts = utils::split(line.substr(7), ' ');
                            for (auto& p : parts) {
                                if (!p.empty() && std::isdigit(p[0])) {
                                    ps.mem_vms_bytes = std::stoull(p) * 1024;
                                    break;
                                }
                            }
                        } catch (...) {}
                    }
                }
            }

            // Memory percentage calculation
            auto meminfo = utils::read_file("/proc/meminfo");
            if (meminfo.has_value()) {
                std::istringstream mi(meminfo.value());
                std::string ml;
                while (std::getline(mi, ml)) {
                    if (ml.substr(0, 8) == "MemTotal") {
                        auto p = utils::split(ml.substr(8), ' ');
                        for (auto& x : p) {
                            if (!x.empty() && std::isdigit(x[0])) {
                                uint64_t total_ram = std::stoull(x) * 1024;
                                if (total_ram > 0) {
                                    ps.mem_percent = static_cast<double>(ps.mem_rss_bytes) /
                                                     static_cast<double>(total_ram) * 100.0;
                                }
                                break;
                            }
                        }
                        break;
                    }
                }
            }

            (void)page_s; (void)hz;
            result.push_back(ps);
        }
    } catch (...) {}

    // Cleanup dead processes from previous_snapshots_
    if (previous_snapshots_.size() > 2000) {
        previous_snapshots_.clear();
    }

    std::sort(result.begin(), result.end(), [](const ProcessStats& a, const ProcessStats& b) {
        if (a.cpu_percent != b.cpu_percent) return a.cpu_percent > b.cpu_percent;
        if (a.mem_rss_bytes != b.mem_rss_bytes) return a.mem_rss_bytes > b.mem_rss_bytes;
        return a.pid < b.pid;
    });

    if (limit > 0 && result.size() > limit) {
        result.resize(limit);
    }

    return result;
}

#else
std::vector<ProcessStats> ProcessMonitor::read_linux(unsigned int) { return {}; }
#endif

// ---------------------------------------------------------------------------
// macOS implementation using libproc
// ---------------------------------------------------------------------------

#if defined(SYSMON_MACOS)

std::vector<ProcessStats> ProcessMonitor::read_macos(unsigned int limit) {
    std::vector<ProcessStats> result;

    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
    size_t size = 0;
    if (sysctl(mib, 4, nullptr, &size, nullptr, 0) != 0 || size == 0) return result;

    std::vector<struct kinfo_proc> procs(size / sizeof(struct kinfo_proc));
    if (sysctl(mib, 4, procs.data(), &size, nullptr, 0) != 0) return result;

    // Get total physical memory for calculating mem_percent
    uint64_t total_ram = 0;
    int mem_mib[2] = {CTL_HW, HW_MEMSIZE};
    size_t mem_len = sizeof(total_ram);
    sysctl(mem_mib, 2, &total_ram, &mem_len, nullptr, 0);

    auto now = std::chrono::steady_clock::now();

    for (const auto& kp : procs) {
        int pid = kp.kp_proc.p_pid;
        if (pid < 0) continue;

        ProcessStats ps;
        ps.pid  = pid;
        ps.user = get_username(kp.kp_eproc.e_ucred.cr_uid);

        // Process name via proc_name or kinfo_proc fallback
        char name_buf[PROC_PIDPATHINFO_MAXSIZE] = {0};
        if (proc_name(pid, name_buf, sizeof(name_buf)) > 0 && name_buf[0] != '\0') {
            ps.name = name_buf;
        } else if (kp.kp_proc.p_comm[0] != '\0') {
            ps.name = kp.kp_proc.p_comm;
        } else {
            ps.name = std::to_string(pid);
        }

        // Process state char
        char state = kp.kp_proc.p_stat;
        if (state == SRUN)       ps.state = "R";
        else if (state == SSLEEP) ps.state = "S";
        else if (state == SSTOP)  ps.state = "T";
        else if (state == SZOMB)  ps.state = "Z";
        else                     ps.state = "S";

        // Query taskinfo via proc_pidinfo
        struct proc_taskinfo pti{};
        int ret = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &pti, sizeof(pti));
        if (ret == sizeof(pti)) {
            ps.mem_rss_bytes = pti.pti_resident_size;
            ps.mem_vms_bytes = pti.pti_virtual_size;
            ps.threads       = static_cast<unsigned int>(pti.pti_threadnum);

            if (total_ram > 0) {
                ps.mem_percent = (static_cast<double>(ps.mem_rss_bytes) / static_cast<double>(total_ram)) * 100.0;
            }

            // CPU percentage from total user + system time in nanoseconds
            uint64_t total_time_ns = pti.pti_total_user + pti.pti_total_system;
            auto it = previous_snapshots_.find(pid);
            if (it != previous_snapshots_.end()) {
                double dt = std::chrono::duration<double>(now - it->second.timestamp).count();
                if (dt > 0.0) {
                    uint64_t prev_time = it->second.utime; // utime used as total_time_ns
                    if (total_time_ns >= prev_time) {
                        double delta_ns = static_cast<double>(total_time_ns - prev_time);
                        double delta_sec = delta_ns / 1e9;
                        ps.cpu_percent = (delta_sec / dt) * 100.0;
                        if (ps.cpu_percent < 0.0) ps.cpu_percent = 0.0;
                    }
                }
            }

            ProcSnapshot snap;
            snap.utime     = total_time_ns;
            snap.stime     = 0;
            snap.timestamp = now;
            previous_snapshots_[pid] = snap;
        }

        result.push_back(ps);
    }

    // Cleanup dead processes from cache if too large
    if (previous_snapshots_.size() > 2000) {
        previous_snapshots_.clear();
    }

    std::sort(result.begin(), result.end(), [](const ProcessStats& a, const ProcessStats& b) {
        if (a.cpu_percent != b.cpu_percent) return a.cpu_percent > b.cpu_percent;
        if (a.mem_rss_bytes != b.mem_rss_bytes) return a.mem_rss_bytes > b.mem_rss_bytes;
        return a.pid < b.pid;
    });

    if (limit > 0 && result.size() > limit) {
        result.resize(limit);
    }

    return result;
}

#else
std::vector<ProcessStats> ProcessMonitor::read_macos(unsigned int) { return {}; }
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string ProcessMonitor::get_username(unsigned int uid) {
    struct passwd* pw = getpwuid(uid);
    if (pw != nullptr) return pw->pw_name;
    return std::to_string(uid);
}

std::string ProcessMonitor::read_proc_name(int pid) {
#if defined(SYSMON_LINUX)
    auto comm = utils::read_file("/proc/" + std::to_string(pid) + "/comm");
    if (comm.has_value()) return utils::trim(comm.value());
#elif defined(SYSMON_MACOS)
    char buf[PROC_PIDPATHINFO_MAXSIZE] = {0};
    if (proc_name(pid, buf, sizeof(buf)) > 0) return buf;
#endif
    return std::to_string(pid);
}
