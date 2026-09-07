#include "sysmon/process_monitor.hpp"
#include "sysmon/platform.hpp"
#include "sysmon/utils.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>
#include <system_error>

#if defined(SYSMON_POSIX)
#  include <pwd.h>
#  include <unistd.h>
#endif

#if defined(SYSMON_LINUX)
#  include <filesystem>
#endif

#if defined(SYSMON_MACOS)
#  include <sys/sysctl.h>
#  include <sys/proc_info.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <libproc.h>
#  include <mach/mach.h>
#  include <mach/mach_time.h>
#endif

#if defined(SYSMON_WINDOWS)
#  include <windows.h>
#  include <tlhelp32.h>
#  include <psapi.h>
#endif

namespace {

constexpr size_t kMaxSnapshotEntries = 8192;

#if defined(SYSMON_MACOS)

/// Nanoseconds per mach absolute-time tick.
///
/// proc_pidinfo() reports task times in mach ticks, which equal nanoseconds on
/// Intel but not on Apple Silicon (the timebase there is 125/3).  Treating the
/// raw value as nanoseconds understates every process's CPU time by ~41x.
double mach_ticks_to_ns() {
    static const double factor = [] {
        mach_timebase_info_data_t timebase{};
        if (mach_timebase_info(&timebase) != KERN_SUCCESS || timebase.denom == 0) {
            return 1.0;
        }
        return static_cast<double>(timebase.numer) / static_cast<double>(timebase.denom);
    }();
    return factor;
}

#endif // SYSMON_MACOS

#if defined(SYSMON_WINDOWS)

/// Convert a FILETIME pair to 100 ns ticks.
uint64_t filetime_to_ticks(const FILETIME& ft) {
    ULARGE_INTEGER value;
    value.LowPart  = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

/// Resolve the account name owning a process handle.
std::string process_owner(HANDLE process) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return "";

    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0) {
        CloseHandle(token);
        return "";
    }

    std::vector<char> buffer(size);
    std::string owner;
    if (GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        auto* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
        char name[256]   = {};
        char domain[256] = {};
        DWORD name_len   = sizeof(name);
        DWORD domain_len = sizeof(domain);
        SID_NAME_USE use{};
        if (LookupAccountSidA(nullptr, user->User.Sid, name, &name_len,
                              domain, &domain_len, &use)) {
            owner = name;
        }
    }
    CloseHandle(token);
    return owner;
}

#endif // SYSMON_WINDOWS

} // namespace

ProcessMonitor::ProcessMonitor() = default;

std::vector<ProcessStats> ProcessMonitor::read(unsigned int limit, ProcSort sort) {
#if defined(SYSMON_LINUX)
    auto result = read_linux(0);
#elif defined(SYSMON_MACOS)
    auto result = read_macos(0);
#elif defined(SYSMON_WINDOWS)
    auto result = read_windows(0);
#else
    std::vector<ProcessStats> result;
#endif

    prune_snapshots(result);
    sort_processes(result, sort);

    if (limit > 0 && result.size() > limit) {
        result.resize(limit);
    }
    return result;
}

void ProcessMonitor::sort_processes(std::vector<ProcessStats>& procs, ProcSort sort) {
    switch (sort) {
        case ProcSort::Memory:
            std::sort(procs.begin(), procs.end(), [](const ProcessStats& a, const ProcessStats& b) {
                if (a.mem_rss_bytes != b.mem_rss_bytes) return a.mem_rss_bytes > b.mem_rss_bytes;
                return a.pid < b.pid;
            });
            break;
        case ProcSort::Pid:
            std::sort(procs.begin(), procs.end(), [](const ProcessStats& a, const ProcessStats& b) {
                return a.pid < b.pid;
            });
            break;
        case ProcSort::Name:
            std::sort(procs.begin(), procs.end(), [](const ProcessStats& a, const ProcessStats& b) {
                const std::string an = utils::to_lower(a.name);
                const std::string bn = utils::to_lower(b.name);
                if (an != bn) return an < bn;
                return a.pid < b.pid;
            });
            break;
        case ProcSort::Time:
            std::sort(procs.begin(), procs.end(), [](const ProcessStats& a, const ProcessStats& b) {
                if (a.cpu_time_seconds != b.cpu_time_seconds) {
                    return a.cpu_time_seconds > b.cpu_time_seconds;
                }
                return a.pid < b.pid;
            });
            break;
        case ProcSort::Cpu:
        default:
            std::sort(procs.begin(), procs.end(), [](const ProcessStats& a, const ProcessStats& b) {
                if (a.cpu_percent != b.cpu_percent)       return a.cpu_percent > b.cpu_percent;
                if (a.mem_rss_bytes != b.mem_rss_bytes)   return a.mem_rss_bytes > b.mem_rss_bytes;
                return a.pid < b.pid;
            });
            break;
    }
}

void ProcessMonitor::prune_snapshots(const std::vector<ProcessStats>& live) {
    if (previous_snapshots_.size() <= kMaxSnapshotEntries) return;

    std::set<int> alive;
    for (const auto& p : live) alive.insert(p.pid);

    for (auto it = previous_snapshots_.begin(); it != previous_snapshots_.end(); ) {
        it = (alive.count(it->first) == 0) ? previous_snapshots_.erase(it) : std::next(it);
    }
}

// ---------------------------------------------------------------------------
// Linux
// ---------------------------------------------------------------------------

#if defined(SYSMON_LINUX)

std::vector<ProcessStats> ProcessMonitor::read_linux(unsigned int limit) {
    namespace fs = std::filesystem;

    const auto now = std::chrono::steady_clock::now();
    const long hz  = sysconf(_SC_CLK_TCK) > 0 ? sysconf(_SC_CLK_TCK) : 100;

    // Machine-wide CPU time, used as the denominator for every process.
    unsigned long long total_cpu = 0;
    if (auto stat_file = utils::read_file("/proc/stat")) {
        std::istringstream iss(stat_file.value());
        std::string line;
        std::getline(iss, line);   // the aggregate "cpu " line
        const auto parts = utils::split_whitespace(line);
        for (size_t i = 1; i < parts.size(); ++i) {
            const auto v = utils::to_int(parts[i]);
            if (v.has_value() && *v >= 0) total_cpu += static_cast<unsigned long long>(*v);
        }
    }

    unsigned int logical_cores = 1;
    if (auto cpuinfo = utils::read_file("/proc/cpuinfo")) {
        std::istringstream iss(cpuinfo.value());
        std::string line;
        unsigned int count = 0;
        while (std::getline(iss, line)) {
            if (utils::starts_with(line, "processor")) ++count;
        }
        if (count > 0) logical_cores = count;
    }

    // Hoisted out of the per-process loop: this used to be re-read and
    // re-parsed once for every process on the system.
    uint64_t total_ram = 0;
    if (auto meminfo = utils::read_file("/proc/meminfo")) {
        std::istringstream iss(meminfo.value());
        std::string line;
        while (std::getline(iss, line)) {
            if (!utils::starts_with(line, "MemTotal")) continue;
            const auto parts = utils::split_whitespace(line);
            if (parts.size() >= 2) {
                if (auto kb = utils::to_int(parts[1])) {
                    if (*kb > 0) total_ram = static_cast<uint64_t>(*kb) * 1024;
                }
            }
            break;
        }
    }

    // Boot time, so a process start time can be turned into a wall-clock date.
    long long boot_time_epoch = 0;
    if (auto stat_file = utils::read_file("/proc/stat")) {
        std::istringstream iss(stat_file.value());
        std::string line;
        while (std::getline(iss, line)) {
            if (!utils::starts_with(line, "btime")) continue;
            const auto parts = utils::split_whitespace(line);
            if (parts.size() >= 2) boot_time_epoch = utils::to_int(parts[1]).value_or(0);
            break;
        }
    }

    const double cpu_delta = static_cast<double>(total_cpu) -
                             static_cast<double>(total_cpu_time_prev_);
    total_cpu_time_prev_ = total_cpu;

    std::vector<ProcessStats> result;
    std::error_code ec;

    for (const auto& entry : fs::directory_iterator("/proc", ec)) {
        if (ec) break;

        const std::string fname = entry.path().filename().string();
        if (fname.empty() || !std::isdigit(static_cast<unsigned char>(fname[0]))) continue;

        const auto pid_value = utils::to_int(fname);
        if (!pid_value.has_value()) continue;
        const int pid = static_cast<int>(*pid_value);

        const std::string proc_dir = "/proc/" + fname;

        auto stat = utils::read_file(proc_dir + "/stat");
        if (!stat.has_value()) continue;

        // The comm field is parenthesised and may itself contain spaces and
        // parentheses, so split around the *last* ')'.
        const std::string& s = stat.value();
        const size_t comm_start = s.find('(');
        const size_t comm_end   = s.rfind(')');
        if (comm_start == std::string::npos || comm_end == std::string::npos ||
            comm_end < comm_start) {
            continue;
        }

        ProcessStats ps;
        ps.pid  = pid;
        ps.name = s.substr(comm_start + 1, comm_end - comm_start - 1);

        // fields[0] is state, i.e. field 3 of proc(5).
        const auto fields = utils::split_whitespace(s.substr(comm_end + 1));
        if (fields.size() < 20) continue;

        auto field_u = [&fields](size_t i) -> unsigned long long {
            if (i >= fields.size()) return 0;
            const auto v = utils::to_int(fields[i]);
            return (v.has_value() && *v >= 0) ? static_cast<unsigned long long>(*v) : 0ULL;
        };

        ps.state   = fields[0];
        ps.ppid    = static_cast<int>(field_u(1));
        const unsigned long long utime = field_u(11);
        const unsigned long long stime = field_u(12);
        if (fields.size() > 16) {
            if (auto nice = utils::to_int(fields[16])) ps.nice = static_cast<int>(*nice);
        }
        ps.threads = static_cast<unsigned int>(field_u(17));
        ps.cpu_time_seconds = static_cast<double>(utime + stime) / static_cast<double>(hz);
        if (boot_time_epoch > 0) {
            ps.start_time = boot_time_epoch +
                            static_cast<long long>(field_u(19) / static_cast<unsigned long long>(hz));
        }

        auto it = previous_snapshots_.find(pid);
        if (it != previous_snapshots_.end() && cpu_delta > 0) {
            const double proc_delta = static_cast<double>(utime + stime) -
                                      static_cast<double>(it->second.utime + it->second.stime);
            if (proc_delta > 0) {
                ps.cpu_percent = (proc_delta / cpu_delta) * 100.0 *
                                 static_cast<double>(logical_cores);
                ps.cpu_percent = std::min(ps.cpu_percent, 100.0 * static_cast<double>(logical_cores));
            }
        }

        // -- status: uid, memory --------------------------------------------
        if (auto status = utils::read_file(proc_dir + "/status")) {
            std::istringstream ss(status.value());
            std::string line;
            while (std::getline(ss, line)) {
                const auto parts = utils::split_whitespace(line);
                if (parts.size() < 2) continue;

                if (parts[0] == "Uid:") {
                    if (auto uid = utils::to_int(parts[1])) {
                        ps.user = get_username(static_cast<unsigned int>(*uid));
                    }
                } else if (parts[0] == "VmRSS:") {
                    if (auto kb = utils::to_int(parts[1])) {
                        if (*kb >= 0) ps.mem_rss_bytes = static_cast<uint64_t>(*kb) * 1024;
                    }
                } else if (parts[0] == "VmSize:") {
                    if (auto kb = utils::to_int(parts[1])) {
                        if (*kb >= 0) ps.mem_vms_bytes = static_cast<uint64_t>(*kb) * 1024;
                    }
                }
            }
        }

        if (total_ram > 0) {
            ps.mem_percent = static_cast<double>(ps.mem_rss_bytes) /
                             static_cast<double>(total_ram) * 100.0;
        }

        // -- cmdline ---------------------------------------------------------
        if (auto cmdline = utils::read_file(proc_dir + "/cmdline")) {
            // Arguments are NUL separated; render them space separated.
            std::string joined = cmdline.value();
            std::replace(joined.begin(), joined.end(), '\0', ' ');
            ps.cmdline = utils::trim(joined);
        }

        // -- per-process I/O (needs matching uid or CAP_SYS_PTRACE) ----------
        uint64_t io_read = 0, io_write = 0;
        bool have_io = false;
        if (auto io = utils::read_file(proc_dir + "/io")) {
            std::istringstream ss(io.value());
            std::string line;
            while (std::getline(ss, line)) {
                const auto parts = utils::split_whitespace(line);
                if (parts.size() < 2) continue;
                if (parts[0] == "read_bytes:") {
                    io_read = static_cast<uint64_t>(utils::to_int(parts[1]).value_or(0));
                    have_io = true;
                } else if (parts[0] == "write_bytes:") {
                    io_write = static_cast<uint64_t>(utils::to_int(parts[1]).value_or(0));
                    have_io = true;
                }
            }
        }

        // -- open file descriptors ------------------------------------------
        {
            std::error_code fd_ec;
            uint64_t fds = 0;
            for (const auto& fd : fs::directory_iterator(proc_dir + "/fd", fd_ec)) {
                if (fd_ec) break;
                (void)fd;
                ++fds;
            }
            if (!fd_ec) ps.open_files = fds;
        }

        if (have_io) {
            // The cumulative counters answer "how much has this process
            // written since it started", which the instantaneous rate cannot.
            ps.io_read_bytes_total  = io_read;
            ps.io_write_bytes_total = io_write;
        }

        if (it != previous_snapshots_.end() && have_io) {
            const double dt = std::chrono::duration<double>(now - it->second.timestamp).count();
            if (dt > 0.0) {
                if (io_read >= it->second.io_read_bytes) {
                    ps.io_read_bytes_per_sec =
                        static_cast<double>(io_read - it->second.io_read_bytes) / dt;
                }
                if (io_write >= it->second.io_write_bytes) {
                    ps.io_write_bytes_per_sec =
                        static_cast<double>(io_write - it->second.io_write_bytes) / dt;
                }
            }
        }

        ProcSnapshot snap;
        snap.utime          = utime;
        snap.stime          = stime;
        snap.io_read_bytes  = io_read;
        snap.io_write_bytes = io_write;
        snap.timestamp      = now;
        previous_snapshots_[pid] = snap;

        result.push_back(std::move(ps));
    }

    if (limit > 0 && result.size() > limit) result.resize(limit);
    return result;
}

#else
std::vector<ProcessStats> ProcessMonitor::read_linux(unsigned int) { return {}; }
#endif

// ---------------------------------------------------------------------------
// macOS
// ---------------------------------------------------------------------------

#if defined(SYSMON_MACOS)

std::vector<ProcessStats> ProcessMonitor::read_macos(unsigned int limit) {
    std::vector<ProcessStats> result;

    // The process table can change size between the sizing and the fill call,
    // so retry rather than trusting a single measurement.
    std::vector<struct kinfo_proc> procs;
    for (int attempt = 0; attempt < 3; ++attempt) {
        int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
        size_t size = 0;
        if (sysctl(mib, 4, nullptr, &size, nullptr, 0) != 0 || size == 0) return result;

        const size_t count = size / sizeof(struct kinfo_proc);
        if (count == 0) return result;
        procs.resize(count + 32);
        size = procs.size() * sizeof(struct kinfo_proc);
        if (sysctl(mib, 4, procs.data(), &size, nullptr, 0) == 0) {
            procs.resize(size / sizeof(struct kinfo_proc));
            break;
        }
        procs.clear();
    }
    if (procs.empty()) return result;

    uint64_t total_ram = 0;
    {
        int mem_mib[2] = {CTL_HW, HW_MEMSIZE};
        size_t mem_len = sizeof(total_ram);
        sysctl(mem_mib, 2, &total_ram, &mem_len, nullptr, 0);
    }

    const auto now = std::chrono::steady_clock::now();

    /// Scratch space for the per-process descriptor-table fetch, reused across
    /// the whole list so counting open files costs one allocation, not one per
    /// process.
    std::vector<proc_fdinfo> fd_buffer;

    for (const auto& kp : procs) {
        const int pid = kp.kp_proc.p_pid;
        if (pid < 0) continue;

        ProcessStats ps;
        ps.pid  = pid;
        ps.ppid = kp.kp_eproc.e_ppid;
        ps.user = get_username(kp.kp_eproc.e_ucred.cr_uid);
        ps.nice = kp.kp_proc.p_nice;
        ps.start_time = kp.kp_proc.p_starttime.tv_sec;

        char name_buf[PROC_PIDPATHINFO_MAXSIZE] = {};
        if (proc_name(pid, name_buf, sizeof(name_buf)) > 0 && name_buf[0] != '\0') {
            ps.name = name_buf;
        } else if (kp.kp_proc.p_comm[0] != '\0') {
            ps.name = kp.kp_proc.p_comm;
        } else {
            ps.name = std::to_string(pid);
        }

        // Full executable path, the closest macOS equivalent of a cmdline that
        // is readable without elevated privileges.
        char path_buf[PROC_PIDPATHINFO_MAXSIZE] = {};
        if (proc_pidpath(pid, path_buf, sizeof(path_buf)) > 0) {
            ps.cmdline = path_buf;
        }

        switch (kp.kp_proc.p_stat) {
            case SRUN:   ps.state = "R"; break;   // refined below via pti_numrunning
            case SSLEEP: ps.state = "S"; break;
            case SSTOP:  ps.state = "T"; break;
            case SZOMB:  ps.state = "Z"; break;
            case SIDL:   ps.state = "I"; break;
            default:     ps.state = "S"; break;
        }

        struct proc_taskinfo pti{};
        const int ret = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &pti, sizeof(pti));
        if (ret == sizeof(pti)) {
            ps.mem_rss_bytes = pti.pti_resident_size;
            ps.mem_vms_bytes = pti.pti_virtual_size;
            ps.threads       = static_cast<unsigned int>(pti.pti_threadnum);

            if (total_ram > 0) {
                ps.mem_percent = static_cast<double>(ps.mem_rss_bytes) /
                                 static_cast<double>(total_ram) * 100.0;
            }

            const uint64_t total_ticks = pti.pti_total_user + pti.pti_total_system;
            const double total_time_ns = static_cast<double>(total_ticks) * mach_ticks_to_ns();
            ps.cpu_time_seconds = total_time_ns / 1e9;

            // kinfo_proc marks every macOS process SRUN, so the real run state
            // comes from whether the task currently has a running thread.
            if (ps.state == "R" && pti.pti_numrunning == 0) ps.state = "S";

            // Open descriptor count.
            //
            // The size query is the descriptor *table's* capacity, not how
            // much of it is in use: for one Firefox process it answered 6560
            // bytes (820 entries) while the filled call returned 920 bytes
            // (115 entries).  Only the second number is the count, so the
            // table has to be fetched, not just measured.  The buffer is
            // reused across processes to keep that to one allocation.
            const int fd_capacity = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
            if (fd_capacity > 0) {
                const size_t needed = static_cast<size_t>(fd_capacity) / sizeof(proc_fdinfo) + 1;
                if (fd_buffer.size() < needed) fd_buffer.resize(needed);
                const int fd_used = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fd_buffer.data(),
                                                 static_cast<int>(fd_buffer.size() * sizeof(proc_fdinfo)));
                if (fd_used > 0) {
                    ps.open_files = static_cast<uint64_t>(fd_used) / sizeof(proc_fdinfo);
                }
            }

            // Per-process disk I/O, when the kernel will report it for us.
            uint64_t io_read = 0, io_write = 0;
            bool have_io = false;
            rusage_info_current rusage{};
            if (proc_pid_rusage(pid, RUSAGE_INFO_CURRENT,
                                reinterpret_cast<rusage_info_t*>(&rusage)) == 0) {
                io_read  = rusage.ri_diskio_bytesread;
                io_write = rusage.ri_diskio_byteswritten;
                have_io  = true;
                // Cumulative since the process started, which is the half of
                // "how much is this program writing" that a rate cannot give.
                ps.io_read_bytes_total  = io_read;
                ps.io_write_bytes_total = io_write;
            }

            auto it = previous_snapshots_.find(pid);
            if (it != previous_snapshots_.end()) {
                const double dt = std::chrono::duration<double>(now - it->second.timestamp).count();
                if (dt > 0.0) {
                    if (total_ticks >= it->second.utime) {
                        const double delta_sec =
                            static_cast<double>(total_ticks - it->second.utime) *
                            mach_ticks_to_ns() / 1e9;
                        ps.cpu_percent = (delta_sec / dt) * 100.0;
                    }
                    if (have_io) {
                        if (io_read >= it->second.io_read_bytes) {
                            ps.io_read_bytes_per_sec =
                                static_cast<double>(io_read - it->second.io_read_bytes) / dt;
                        }
                        if (io_write >= it->second.io_write_bytes) {
                            ps.io_write_bytes_per_sec =
                                static_cast<double>(io_write - it->second.io_write_bytes) / dt;
                        }
                    }
                }
            }

            ProcSnapshot snap;
            snap.utime          = total_ticks;
            snap.stime          = 0;
            snap.io_read_bytes  = io_read;
            snap.io_write_bytes = io_write;
            snap.timestamp      = now;
            previous_snapshots_[pid] = snap;
        }

        result.push_back(std::move(ps));
    }

    if (limit > 0 && result.size() > limit) result.resize(limit);
    return result;
}

#else
std::vector<ProcessStats> ProcessMonitor::read_macos(unsigned int) { return {}; }
#endif

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------

#if defined(SYSMON_WINDOWS)

std::vector<ProcessStats> ProcessMonitor::read_windows(unsigned int limit) {
    std::vector<ProcessStats> result;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;

    uint64_t total_ram = 0;
    {
        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms)) total_ram = ms.ullTotalPhys;
    }

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const auto logical_cores = si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1;

    const auto now = std::chrono::steady_clock::now();

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        CloseHandle(snapshot);
        return result;
    }

    do {
        ProcessStats ps;
        ps.pid     = static_cast<int>(entry.th32ProcessID);
        ps.ppid    = static_cast<int>(entry.th32ParentProcessID);
        ps.threads = entry.cntThreads;
        ps.state   = "R";   // Windows has no per-process run state to report.

        {
            const int needed = WideCharToMultiByte(CP_UTF8, 0, entry.szExeFile, -1,
                                                   nullptr, 0, nullptr, nullptr);
            if (needed > 1) {
                std::string name(static_cast<size_t>(needed - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, entry.szExeFile, -1,
                                    name.data(), needed, nullptr, nullptr);
                ps.name = name;
            }
        }

        // PROCESS_QUERY_LIMITED_INFORMATION works for processes owned by other
        // users too, unlike the older PROCESS_QUERY_INFORMATION.
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                                     FALSE, entry.th32ProcessID);
        if (process == nullptr) {
            process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        }

        if (process != nullptr) {
            FILETIME creation{}, exit_time{}, kernel{}, user{};
            if (GetProcessTimes(process, &creation, &exit_time, &kernel, &user)) {
                const uint64_t kernel_ticks = filetime_to_ticks(kernel);
                const uint64_t user_ticks   = filetime_to_ticks(user);
                const uint64_t total_ticks  = kernel_ticks + user_ticks;
                ps.cpu_time_seconds = static_cast<double>(total_ticks) / 1e7;

                // FILETIME epoch is 1601-01-01; Unix epoch is 11644473600 s later.
                const uint64_t created = filetime_to_ticks(creation);
                if (created > 0) {
                    ps.start_time = static_cast<long long>(created / 10'000'000ULL) - 11644473600LL;
                }

                auto it = previous_snapshots_.find(ps.pid);
                if (it != previous_snapshots_.end()) {
                    const double dt = std::chrono::duration<double>(now - it->second.timestamp).count();
                    const uint64_t before = it->second.utime + it->second.stime;
                    if (dt > 0.0 && total_ticks >= before) {
                        const double delta_sec = static_cast<double>(total_ticks - before) / 1e7;
                        ps.cpu_percent = std::min((delta_sec / dt) * 100.0,
                                                  100.0 * static_cast<double>(logical_cores));
                    }
                }

                ProcSnapshot snap;
                snap.utime     = user_ticks;
                snap.stime     = kernel_ticks;
                snap.timestamp = now;

                PROCESS_MEMORY_COUNTERS_EX counters{};
                counters.cb = sizeof(counters);
                if (GetProcessMemoryInfo(process,
                                         reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                         sizeof(counters))) {
                    ps.mem_rss_bytes = counters.WorkingSetSize;
                    ps.mem_vms_bytes = counters.PrivateUsage;
                    if (total_ram > 0) {
                        ps.mem_percent = static_cast<double>(ps.mem_rss_bytes) /
                                         static_cast<double>(total_ram) * 100.0;
                    }
                }

                IO_COUNTERS io{};
                if (GetProcessIoCounters(process, &io)) {
                    // ReadTransferCount / WriteTransferCount are cumulative
                    // for the life of the process, so they are the totals as
                    // well as the source of the rate below.
                    ps.io_read_bytes_total  = io.ReadTransferCount;
                    ps.io_write_bytes_total = io.WriteTransferCount;

                    const auto it2 = previous_snapshots_.find(ps.pid);
                    if (it2 != previous_snapshots_.end()) {
                        const double dt = std::chrono::duration<double>(now - it2->second.timestamp).count();
                        if (dt > 0.0) {
                            if (io.ReadTransferCount >= it2->second.io_read_bytes) {
                                ps.io_read_bytes_per_sec =
                                    static_cast<double>(io.ReadTransferCount - it2->second.io_read_bytes) / dt;
                            }
                            if (io.WriteTransferCount >= it2->second.io_write_bytes) {
                                ps.io_write_bytes_per_sec =
                                    static_cast<double>(io.WriteTransferCount - it2->second.io_write_bytes) / dt;
                            }
                        }
                    }
                    snap.io_read_bytes  = io.ReadTransferCount;
                    snap.io_write_bytes = io.WriteTransferCount;
                }

                previous_snapshots_[ps.pid] = snap;
            }

            DWORD handles = 0;
            if (GetProcessHandleCount(process, &handles)) {
                ps.open_files = handles;
            }

            char image[MAX_PATH] = {};
            DWORD image_size = sizeof(image);
            if (QueryFullProcessImageNameA(process, 0, image, &image_size)) {
                ps.cmdline = image;
            }

            ps.user = process_owner(process);
            CloseHandle(process);
        }

        if (ps.user.empty()) ps.user = "-";
        result.push_back(std::move(ps));
    } while (Process32NextW(snapshot, &entry));

    CloseHandle(snapshot);

    if (limit > 0 && result.size() > limit) result.resize(limit);
    return result;
}

#else
std::vector<ProcessStats> ProcessMonitor::read_windows(unsigned int) { return {}; }
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string ProcessMonitor::get_username(unsigned int uid) {
#if defined(SYSMON_POSIX)
    // Cache lookups: getpwuid() hits the directory service, which is far too
    // expensive to repeat for every process on every refresh.
    static std::map<unsigned int, std::string> cache;
    const auto it = cache.find(uid);
    if (it != cache.end()) return it->second;

    std::string name = std::to_string(uid);
    if (struct passwd* pw = getpwuid(uid)) {
        name = pw->pw_name;
    }
    cache[uid] = name;
    return name;
#else
    return std::to_string(uid);
#endif
}

// ---------------------------------------------------------------------------
// Open descriptors (on demand, one process at a time)
// ---------------------------------------------------------------------------

#if defined(SYSMON_LINUX)

OpenFilesResult ProcessMonitor::open_files(int pid) {
    OpenFilesResult result;
    const std::string dir = "/proc/" + std::to_string(pid) + "/fd";

    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        result.status = OpenFilesStatus::NoSuchProcess;
        return result;
    }

    std::filesystem::directory_iterator it(dir, ec);
    if (ec) {
        // EACCES is by far the common case here: /proc/<pid>/fd is 0500 and
        // owned by the process's user.  Report that as its own answer rather
        // than as "no files open".
        result.status = (ec == std::errc::permission_denied)
                      ? OpenFilesStatus::PermissionDenied
                      : OpenFilesStatus::NoSuchProcess;
        return result;
    }

    for (const auto& entry : it) {
        OpenFile file;
        const std::string name = entry.path().filename().string();
        if (const auto fd = utils::to_int(name)) file.fd = static_cast<int>(*fd);

        std::error_code link_ec;
        const std::filesystem::path target = std::filesystem::read_symlink(entry.path(), link_ec);
        if (link_ec) continue;
        file.path = target.string();

        // The kernel spells non-file descriptors as "socket:[12345]" and
        // friends, which is also how their type is identified.
        if (utils::starts_with(file.path, "socket:"))      file.type = "socket";
        else if (utils::starts_with(file.path, "pipe:"))   file.type = "pipe";
        else if (utils::starts_with(file.path, "anon_inode:")) file.type = "anon";
        else {
            std::error_code stat_ec;
            const auto status = std::filesystem::status(target, stat_ec);
            if (stat_ec)                                            file.type = "file";
            else if (std::filesystem::is_directory(status))         file.type = "dir";
            else if (std::filesystem::is_character_file(status))    file.type = "chr";
            else if (std::filesystem::is_fifo(status))              file.type = "fifo";
            else if (std::filesystem::is_socket(status))            file.type = "socket";
            else                                                     file.type = "file";

            if (file.type == "file") {
                std::error_code size_ec;
                const auto size = std::filesystem::file_size(target, size_ec);
                if (!size_ec) file.size_bytes = static_cast<uint64_t>(size);
            }
        }

        // /proc/<pid>/fdinfo/<fd> carries the access mode and the file offset,
        // which is what says whether a process is reading or writing a path.
        if (const auto info = utils::read_file("/proc/" + std::to_string(pid) +
                                               "/fdinfo/" + name)) {
            std::istringstream iss(*info);
            std::string line;
            while (std::getline(iss, line)) {
                if (utils::starts_with(line, "pos:")) {
                    if (const auto pos = utils::to_int(utils::trim(line.substr(4)))) {
                        file.position = static_cast<uint64_t>(*pos);
                    }
                } else if (utils::starts_with(line, "flags:")) {
                    // Octal open() flags; the low two bits are the access mode.
                    const std::string digits = utils::trim(line.substr(6));
                    const long flags = std::strtol(digits.c_str(), nullptr, 8);
                    switch (flags & 3) {
                        case 0: file.mode = "r";  break;
                        case 1: file.mode = "w";  break;
                        case 2: file.mode = "rw"; break;
                        default: break;
                    }
                }
            }
        }

        result.files.push_back(std::move(file));
    }

    std::sort(result.files.begin(), result.files.end(),
              [](const OpenFile& a, const OpenFile& b) { return a.fd < b.fd; });
    result.status = OpenFilesStatus::Ok;
    return result;
}

#elif defined(SYSMON_MACOS)

OpenFilesResult ProcessMonitor::open_files(int pid) {
    OpenFilesResult result;

    const int size = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
    if (size <= 0) {
        // proc_pidinfo() sets EPERM for a process owned by another user.
        result.status = (errno == EPERM || errno == EACCES)
                      ? OpenFilesStatus::PermissionDenied
                      : OpenFilesStatus::NoSuchProcess;
        return result;
    }

    std::vector<proc_fdinfo> fds(static_cast<size_t>(size) / sizeof(proc_fdinfo) + 1);
    const int used = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds.data(),
                                  static_cast<int>(fds.size() * sizeof(proc_fdinfo)));
    if (used <= 0) {
        result.status = (errno == EPERM || errno == EACCES)
                      ? OpenFilesStatus::PermissionDenied
                      : OpenFilesStatus::NoSuchProcess;
        return result;
    }

    const size_t count = static_cast<size_t>(used) / sizeof(proc_fdinfo);
    for (size_t i = 0; i < count && i < fds.size(); ++i) {
        OpenFile file;
        file.fd = fds[i].proc_fd;

        switch (fds[i].proc_fdtype) {
            case PROX_FDTYPE_VNODE: {
                file.type = "file";
                vnode_fdinfowithpath info{};
                const int n = proc_pidfdinfo(pid, fds[i].proc_fd, PROC_PIDFDVNODEPATHINFO,
                                             &info, sizeof(info));
                if (n == static_cast<int>(sizeof(info))) {
                    file.path = info.pvip.vip_path;
                    if (info.pvip.vip_vi.vi_stat.vst_size > 0) {
                        file.size_bytes = static_cast<uint64_t>(info.pvip.vip_vi.vi_stat.vst_size);
                    }
                    if (S_ISDIR(info.pvip.vip_vi.vi_stat.vst_mode)) file.type = "dir";
                    else if (S_ISCHR(info.pvip.vip_vi.vi_stat.vst_mode)) file.type = "chr";
                    if (info.pfi.fi_offset > 0) {
                        file.position = static_cast<uint64_t>(info.pfi.fi_offset);
                    }
                    switch (info.pfi.fi_openflags & 3) {
                        case 1: file.mode = "r";  break;
                        case 2: file.mode = "w";  break;
                        case 3: file.mode = "rw"; break;
                        default: break;
                    }
                }
                break;
            }
            case PROX_FDTYPE_SOCKET: {
                file.type = "socket";
                socket_fdinfo info{};
                const int n = proc_pidfdinfo(pid, fds[i].proc_fd, PROC_PIDFDSOCKETINFO,
                                             &info, sizeof(info));
                if (n == static_cast<int>(sizeof(info))) {
                    switch (info.psi.soi_family) {
                        case AF_INET:  file.path = "inet socket";  break;
                        case AF_INET6: file.path = "inet6 socket"; break;
                        case AF_UNIX:  file.path = "unix socket";  break;
                        default:       file.path = "socket";       break;
                    }
                } else {
                    file.path = "socket";
                }
                break;
            }
            case PROX_FDTYPE_PIPE:      file.type = "pipe";  file.path = "pipe";  break;
            case PROX_FDTYPE_KQUEUE:    file.type = "kqueue"; file.path = "kqueue"; break;
            case PROX_FDTYPE_ATALK:     file.type = "atalk";  file.path = "atalk";  break;
            default:                    file.type = "other";  file.path = "";       break;
        }

        result.files.push_back(std::move(file));
    }

    std::sort(result.files.begin(), result.files.end(),
              [](const OpenFile& a, const OpenFile& b) { return a.fd < b.fd; });
    result.status = OpenFilesStatus::Ok;
    return result;
}

#elif defined(SYSMON_WINDOWS)

OpenFilesResult ProcessMonitor::open_files(int) {
    // Enumerating another process's handles needs
    // NtQuerySystemInformation(SystemHandleInformation) plus a
    // NtQueryObject() call per handle, and the latter can block indefinitely
    // on a pipe handle with no reader — a documented hang that costs a
    // watchdog thread to work around.  Reporting "unsupported" is the honest
    // answer until that is worth building.
    OpenFilesResult result;
    result.status = OpenFilesStatus::Unsupported;
    return result;
}

#else

OpenFilesResult ProcessMonitor::open_files(int) {
    OpenFilesResult result;
    result.status = OpenFilesStatus::Unsupported;
    return result;
}

#endif
