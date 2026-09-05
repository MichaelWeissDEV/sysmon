#include "sysmon/cpu_monitor.hpp"
#include "sysmon/temperature_monitor.hpp"
#include "sysmon/platform.hpp"
#include "sysmon/utils.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

#if defined(SYSMON_MACOS)
#  include <sys/sysctl.h>
#  include <mach/mach.h>
#  include <mach/processor_info.h>
#  include <mach/mach_host.h>
#  include <notify.h>
#endif

#if defined(SYSMON_WINDOWS)
#  include <windows.h>
#  include <winternl.h>
#  include <powerbase.h>
#endif

#if defined(SYSMON_LINUX)
#  include <filesystem>
#endif

namespace {

constexpr double kMinElapsedSeconds = 1e-6;

#if defined(SYSMON_WINDOWS)

// NtQuerySystemInformation is the only way to get per-processor times on
// Windows; GetSystemTimes reports the machine-wide aggregate only.
using NtQuerySystemInformationFn =
    LONG (WINAPI*)(ULONG SystemInformationClass, PVOID SystemInformation,
                   ULONG SystemInformationLength, PULONG ReturnLength);

constexpr ULONG kSystemProcessorPerformanceInformation = 8;

struct SysProcPerfInfo {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;   // includes IdleTime
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG         InterruptCount;
};

NtQuerySystemInformationFn nt_query_system_information() {
    static NtQuerySystemInformationFn fn = [] () -> NtQuerySystemInformationFn {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr) return nullptr;
        return reinterpret_cast<NtQuerySystemInformationFn>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "NtQuerySystemInformation")));
    }();
    return fn;
}

/// Read a REG_SZ or REG_DWORD value from HKLM.
std::optional<std::string> read_registry_string(const char* subkey, const char* value) {
    HKEY key{};
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    char buf[512] = {};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    const LSTATUS rc = RegQueryValueExA(key, value, nullptr, &type,
                                        reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_SZ) return std::nullopt;
    return utils::trim(std::string(buf, strnlen(buf, size)));
}

std::optional<uint32_t> read_registry_dword(const char* subkey, const char* value) {
    HKEY key{};
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    DWORD data = 0;
    DWORD size = sizeof(data);
    DWORD type = 0;
    const LSTATUS rc = RegQueryValueExA(key, value, nullptr, &type,
                                        reinterpret_cast<LPBYTE>(&data), &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_DWORD) return std::nullopt;
    return static_cast<uint32_t>(data);
}

#endif // SYSMON_WINDOWS

#if defined(SYSMON_MACOS)

/// Read one uint64 sysctl, returning nullopt when the key does not exist.
std::optional<uint64_t> sysctl_u64(const char* name) {
    uint64_t value = 0;
    size_t len = sizeof(value);
    if (sysctlbyname(name, &value, &len, nullptr, 0) != 0) return std::nullopt;
    if (len == sizeof(uint32_t)) {
        uint32_t narrow = 0;
        len = sizeof(narrow);
        if (sysctlbyname(name, &narrow, &len, nullptr, 0) != 0) return std::nullopt;
        return static_cast<uint64_t>(narrow);
    }
    return value;
}

/**
 * @brief macOS thermal pressure level.
 *
 * This is not a temperature — Apple exposes no unprivileged die temperature on
 * Apple Silicon — but it is the OS's own statement about how thermally
 * constrained the machine currently is, which is the actionable part.
 */
std::string thermal_pressure_level() {
    int token = 0;
    if (notify_register_check(kOSThermalNotificationPressureLevelName, &token) != NOTIFY_STATUS_OK) {
        return "";
    }
    uint64_t state = 0;
    const uint32_t rc = notify_get_state(token, &state);
    notify_cancel(token);
    if (rc != NOTIFY_STATUS_OK) return "";

    switch (state) {
        case kOSThermalPressureLevelNominal:  return "Nominal";
        case kOSThermalPressureLevelModerate: return "Moderate";
        case kOSThermalPressureLevelHeavy:    return "Heavy";
        case kOSThermalPressureLevelTrapping: return "Trapping";
        case kOSThermalPressureLevelSleeping: return "Sleeping";
        default: return "";
    }
}

#endif // SYSMON_MACOS

} // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

CpuMonitor::CpuMonitor() = default;

CpuStats CpuMonitor::read() {
    CpuStats stats;

    stats.model             = get_cpu_model().value_or("unknown");
    stats.logical_cores     = get_logical_cores().value_or(0);
    stats.physical_cores    = get_physical_cores().value_or(0);
    stats.max_frequency_mhz = get_max_frequency();

    const auto now = std::chrono::steady_clock::now();

#if defined(SYSMON_LINUX)
    auto cur_times = read_proc_stat_linux();
#elif defined(SYSMON_MACOS)
    auto cur_times = read_cpu_times_macos();
#elif defined(SYSMON_WINDOWS)
    auto cur_times = read_cpu_times_windows();
#else
    std::vector<CpuTimes> cur_times;
#endif

    if (!first_read_ && !cur_times.empty() && !prev_times_.empty()) {
        CpuTimes agg_cur{}, agg_prev{};
        for (const auto& t : cur_times) {
            agg_cur.user    += t.user;
            agg_cur.nice    += t.nice;
            agg_cur.system  += t.system;
            agg_cur.idle    += t.idle;
            agg_cur.iowait  += t.iowait;
            agg_cur.irq     += t.irq;
            agg_cur.softirq += t.softirq;
            agg_cur.steal   += t.steal;
        }
        for (size_t i = 0; i < prev_times_.size() && i < cur_times.size(); ++i) {
            agg_prev.user    += prev_times_[i].user;
            agg_prev.nice    += prev_times_[i].nice;
            agg_prev.system  += prev_times_[i].system;
            agg_prev.idle    += prev_times_[i].idle;
            agg_prev.iowait  += prev_times_[i].iowait;
            agg_prev.irq     += prev_times_[i].irq;
            agg_prev.softirq += prev_times_[i].softirq;
            agg_prev.steal   += prev_times_[i].steal;
        }

        Breakdown total{};
        stats.usage_percent  = usage_from_delta(agg_prev, agg_cur, total);
        stats.user_percent   = total.user;
        stats.system_percent = total.system;
        stats.iowait_percent = total.iowait;
        stats.idle_percent   = total.idle;
        stats.nice_percent   = total.nice;
        stats.irq_percent    = total.irq;
        stats.steal_percent  = total.steal;

        stats.per_core.resize(cur_times.size());
        for (size_t i = 0; i < cur_times.size(); ++i) {
            stats.per_core[i].id = static_cast<unsigned int>(i);
            if (i < prev_times_.size()) {
                Breakdown core{};
                stats.per_core[i].usage_percent =
                    usage_from_delta(prev_times_[i], cur_times[i], core);
                stats.per_core[i].user_percent   = core.user;
                stats.per_core[i].system_percent = core.system;
                stats.per_core[i].iowait_percent = core.iowait;
                stats.per_core[i].idle_percent   = core.idle;
                stats.per_core[i].nice_percent   = core.nice;
                stats.per_core[i].irq_percent    = core.irq;
                stats.per_core[i].steal_percent  = core.steal;
            }
        }
    } else if (!cur_times.empty()) {
        // First sample: publish the core list so the layout is stable from the
        // very first frame, with usage left at zero.
        stats.per_core.resize(cur_times.size());
        for (size_t i = 0; i < cur_times.size(); ++i) {
            stats.per_core[i].id = static_cast<unsigned int>(i);
        }
    }

    const double elapsed = first_read_
        ? 0.0
        : std::chrono::duration<double>(now - prev_ts_).count();

    prev_times_ = cur_times;
    prev_ts_    = now;
    first_read_ = false;

    stats.frequency_mhz = get_cpu_frequency();

    fill_static_info(stats);
    fill_kernel_rates(stats, elapsed);

    // -- Per-core frequency ------------------------------------------------
#if defined(SYSMON_LINUX)
    for (size_t i = 0; i < stats.per_core.size(); ++i) {
        const std::string freq_path = "/sys/devices/system/cpu/cpu" +
                                      std::to_string(i) + "/cpufreq/scaling_cur_freq";
        if (auto f = utils::read_first_line(freq_path)) {
            if (auto khz = utils::to_double(*f)) {
                stats.per_core[i].frequency_mhz = khz.value() / 1000.0;
            }
        }
    }
#elif defined(SYSMON_MACOS)
    // Apple Silicon exposes no per-core frequency through a public API, so the
    // field stays empty rather than repeating a machine-wide number per core.
    if (stats.frequency_mhz.has_value()) {
        for (auto& core : stats.per_core) core.frequency_mhz = stats.frequency_mhz;
    }
#endif

    // -- Temperature -------------------------------------------------------
    TemperatureMonitor temp_mon;
    stats.temperature_celsius = temp_mon.read_cpu_temperature();

    return stats;
}

// ---------------------------------------------------------------------------
// Linux implementation
// ---------------------------------------------------------------------------

#if defined(SYSMON_LINUX)

std::vector<CpuMonitor::CpuTimes> CpuMonitor::read_proc_stat_linux() {
    auto stat = utils::read_file("/proc/stat");
    if (!stat.has_value()) return {};

    std::vector<CpuTimes> result;
    std::istringstream iss(stat.value());
    std::string line;

    while (std::getline(iss, line)) {
        if (line.size() < 4) continue;
        if (line.compare(0, 3, "cpu") != 0 || !std::isdigit(static_cast<unsigned char>(line[3]))) {
            continue;
        }
        const auto parts = utils::split_whitespace(line);
        if (parts.size() < 5) continue;

        CpuTimes t;
        auto field = [&parts](size_t i) -> unsigned long long {
            if (i >= parts.size()) return 0;
            const auto v = utils::to_int(parts[i]);
            return v.has_value() && *v >= 0 ? static_cast<unsigned long long>(*v) : 0ULL;
        };
        t.user    = field(1);
        t.nice    = field(2);
        t.system  = field(3);
        t.idle    = field(4);
        t.iowait  = field(5);
        t.irq     = field(6);
        t.softirq = field(7);
        t.steal   = field(8);
        result.push_back(t);
    }
    return result;
}

#else
std::vector<CpuMonitor::CpuTimes> CpuMonitor::read_proc_stat_linux() { return {}; }
#endif

// ---------------------------------------------------------------------------
// macOS implementation
// ---------------------------------------------------------------------------

#if defined(SYSMON_MACOS)

std::vector<CpuMonitor::CpuTimes> CpuMonitor::read_cpu_times_macos() {
    natural_t cpu_count = 0;
    processor_info_array_t cpu_info = nullptr;
    mach_msg_type_number_t info_count = 0;

    const kern_return_t kr = host_processor_info(mach_host_self(),
                                                 PROCESSOR_CPU_LOAD_INFO,
                                                 &cpu_count, &cpu_info, &info_count);
    if (kr != KERN_SUCCESS) return {};

    std::vector<CpuTimes> result;
    result.reserve(cpu_count);
    for (natural_t i = 0; i < cpu_count; ++i) {
        CpuTimes t;
        t.user   = static_cast<unsigned long long>(cpu_info[CPU_STATE_MAX * i + CPU_STATE_USER]);
        t.nice   = static_cast<unsigned long long>(cpu_info[CPU_STATE_MAX * i + CPU_STATE_NICE]);
        t.system = static_cast<unsigned long long>(cpu_info[CPU_STATE_MAX * i + CPU_STATE_SYSTEM]);
        t.idle   = static_cast<unsigned long long>(cpu_info[CPU_STATE_MAX * i + CPU_STATE_IDLE]);
        result.push_back(t);
    }

    vm_deallocate(mach_task_self(),
                  reinterpret_cast<vm_address_t>(cpu_info),
                  static_cast<vm_size_t>(info_count * sizeof(*cpu_info)));
    return result;
}

#else
std::vector<CpuMonitor::CpuTimes> CpuMonitor::read_cpu_times_macos() { return {}; }
#endif

// ---------------------------------------------------------------------------
// Windows implementation
// ---------------------------------------------------------------------------

#if defined(SYSMON_WINDOWS)

std::vector<CpuMonitor::CpuTimes> CpuMonitor::read_cpu_times_windows() {
    auto query = nt_query_system_information();
    if (query == nullptr) return {};

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const ULONG count = si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1;

    std::vector<SysProcPerfInfo> info(count);
    ULONG returned = 0;
    const LONG status = query(kSystemProcessorPerformanceInformation, info.data(),
                              static_cast<ULONG>(info.size() * sizeof(SysProcPerfInfo)),
                              &returned);
    if (status < 0 || returned == 0) return {};

    const size_t n = std::min<size_t>(info.size(), returned / sizeof(SysProcPerfInfo));
    std::vector<CpuTimes> result;
    result.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const auto idle   = static_cast<unsigned long long>(info[i].IdleTime.QuadPart);
        const auto kernel = static_cast<unsigned long long>(info[i].KernelTime.QuadPart);
        const auto user   = static_cast<unsigned long long>(info[i].UserTime.QuadPart);
        const auto dpc    = static_cast<unsigned long long>(info[i].DpcTime.QuadPart);
        const auto intr   = static_cast<unsigned long long>(info[i].InterruptTime.QuadPart);

        CpuTimes t;
        t.user   = user;
        t.idle   = idle;
        // KernelTime includes IdleTime on Windows, so subtract it out to get
        // the time the kernel actually spent doing work.
        t.system = kernel > idle ? kernel - idle : 0;
        // DPC and interrupt time are already counted inside KernelTime; report
        // them separately for the breakdown but do not double-count the total.
        t.irq     = intr;
        t.softirq = dpc;
        result.push_back(t);
    }
    return result;
}

#else
std::vector<CpuMonitor::CpuTimes> CpuMonitor::read_cpu_times_windows() { return {}; }
#endif

// ---------------------------------------------------------------------------
// Delta arithmetic
// ---------------------------------------------------------------------------

double CpuMonitor::usage_from_delta(const CpuTimes& a, const CpuTimes& b,
                                    double* user_pct, double* sys_pct,
                                    double* iowait_pct, double* idle_pct) {
    Breakdown out{};
    const double usage = usage_from_delta(a, b, out);
    if (user_pct)   *user_pct   = out.user;
    if (sys_pct)    *sys_pct    = out.system;
    if (iowait_pct) *iowait_pct = out.iowait;
    if (idle_pct)   *idle_pct   = out.idle;
    return usage;
}

double CpuMonitor::usage_from_delta(const CpuTimes& a, const CpuTimes& b, Breakdown& out) {
    const auto total_a = a.user + a.nice + a.system + a.idle + a.iowait + a.irq + a.softirq + a.steal;
    const auto total_b = b.user + b.nice + b.system + b.idle + b.iowait + b.irq + b.softirq + b.steal;

    out = Breakdown{};

    // Counter reset / wrap between samples: treat as no useful delta.
    if (total_b < total_a) {
        out.idle = 100.0;
        return 0.0;
    }

    const double delta_total = static_cast<double>(total_b) - static_cast<double>(total_a);
    if (delta_total <= 0) {
        out.idle = 100.0;
        return 0.0;
    }

    auto delta = [](unsigned long long x, unsigned long long y) {
        return x >= y ? static_cast<double>(x - y) : 0.0;
    };
    auto clamp_pct = [](double v) { return std::max(0.0, std::min(100.0, v)); };
    auto pct = [&](double d) { return clamp_pct(d / delta_total * 100.0); };

    const double delta_idle = delta(b.idle, a.idle);

    out.user   = pct(delta(b.user, a.user) + delta(b.nice, a.nice));
    out.system = pct(delta(b.system, a.system) + delta(b.irq, a.irq) + delta(b.softirq, a.softirq));
    out.iowait = pct(delta(b.iowait, a.iowait));
    out.idle   = pct(delta_idle);
    out.nice   = pct(delta(b.nice, a.nice));
    out.irq    = pct(delta(b.irq, a.irq) + delta(b.softirq, a.softirq));
    out.steal  = pct(delta(b.steal, a.steal));

    const double usage = 100.0 * (1.0 - delta_idle / delta_total);
    return std::round(clamp_pct(usage) * 10.0) / 10.0;
}

CpuMonitor::KernelCounters CpuMonitor::parse_proc_stat_counters(const std::string& content) {
    KernelCounters counters;
    std::istringstream iss(content);
    std::string line;

    while (std::getline(iss, line)) {
        const auto parts = utils::split_whitespace(line);
        if (parts.size() < 2) continue;

        const auto value = utils::to_int(parts[1]);
        if (!value.has_value() || *value < 0) continue;
        const auto v = static_cast<uint64_t>(*value);

        if      (parts[0] == "ctxt")         counters.context_switches = v;
        else if (parts[0] == "intr")         counters.interrupts       = v;
        else if (parts[0] == "processes")    counters.forks            = v;
        else if (parts[0] == "procs_running")counters.procs_running    = v;
        else if (parts[0] == "procs_blocked")counters.procs_blocked    = v;
    }
    return counters;
}

// ---------------------------------------------------------------------------
// Kernel event rates
// ---------------------------------------------------------------------------

void CpuMonitor::fill_kernel_rates(CpuStats& stats, double elapsed_seconds) {
    KernelCounters current;

#if defined(SYSMON_LINUX)
    if (auto content = utils::read_file("/proc/stat")) {
        current = parse_proc_stat_counters(content.value());
    }
#elif defined(SYSMON_WINDOWS)
    // Windows exposes the machine-wide interrupt count per processor.
    if (auto query = nt_query_system_information()) {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        const ULONG count = si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1;
        std::vector<SysProcPerfInfo> info(count);
        ULONG returned = 0;
        if (query(kSystemProcessorPerformanceInformation, info.data(),
                  static_cast<ULONG>(info.size() * sizeof(SysProcPerfInfo)), &returned) >= 0) {
            uint64_t interrupts = 0;
            const size_t n = std::min<size_t>(info.size(), returned / sizeof(SysProcPerfInfo));
            for (size_t i = 0; i < n; ++i) interrupts += info[i].InterruptCount;
            current.interrupts = interrupts;
        }
    }
#endif

    stats.total_context_switches = current.context_switches;
    stats.total_interrupts       = current.interrupts;

    if (elapsed_seconds > kMinElapsedSeconds) {
        auto rate = [elapsed_seconds](const std::optional<uint64_t>& now,
                                      const std::optional<uint64_t>& before)
            -> std::optional<double> {
            if (!now.has_value() || !before.has_value()) return std::nullopt;
            if (*now < *before) return std::nullopt;   // counter reset
            return static_cast<double>(*now - *before) / elapsed_seconds;
        };
        stats.context_switches_per_sec = rate(current.context_switches, prev_counters_.context_switches);
        stats.interrupts_per_sec       = rate(current.interrupts,       prev_counters_.interrupts);
        stats.forks_per_sec            = rate(current.forks,            prev_counters_.forks);
    }

    prev_counters_ = current;
}

// ---------------------------------------------------------------------------
// Static CPU facts (vendor, caches, topology, flags)
// ---------------------------------------------------------------------------

void CpuMonitor::fill_static_info(CpuStats& stats) {
#if defined(SYSMON_LINUX)
    if (auto cpuinfo = utils::read_file("/proc/cpuinfo")) {
        std::istringstream iss(cpuinfo.value());
        std::string line;
        std::vector<std::string> physical_ids;
        while (std::getline(iss, line)) {
            const auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            const std::string key   = utils::trim(line.substr(0, colon));
            const std::string value = utils::trim(line.substr(colon + 1));

            if (key == "vendor_id" && stats.vendor.empty())  stats.vendor = value;
            if (key == "siblings" && !stats.threads_per_core.has_value() &&
                stats.physical_cores > 0) {
                if (auto siblings = utils::to_int(value)) {
                    if (*siblings > 0) {
                        stats.threads_per_core = static_cast<unsigned int>(
                            std::max<long long>(1, *siblings / std::max(1u, stats.physical_cores)));
                    }
                }
            }
            if (key == "physical id" &&
                std::find(physical_ids.begin(), physical_ids.end(), value) == physical_ids.end()) {
                physical_ids.push_back(value);
            }
            if (key == "flags" && stats.flags.empty()) {
                stats.flags = utils::split_whitespace(value);
            }
        }
        if (!physical_ids.empty()) {
            stats.sockets = static_cast<unsigned int>(physical_ids.size());
        }
    }

    // Cache topology from sysfs; index0..index3 are L1d, L1i, L2, L3 on x86 but
    // the level/type files are authoritative, so read those instead of assuming.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path base{"/sys/devices/system/cpu/cpu0/cache"};
        if (fs::exists(base, ec)) {
            for (const auto& entry : fs::directory_iterator(base, ec)) {
                if (ec) break;
                const auto level_s = utils::read_first_line((entry.path() / "level").string());
                const auto type_s  = utils::read_first_line((entry.path() / "type").string());
                const auto size_s  = utils::read_first_line((entry.path() / "size").string());
                if (!level_s || !type_s || !size_s) continue;

                const auto level = utils::to_int(*level_s);
                if (!level.has_value()) continue;

                // Sizes look like "32K" or "1024K" or "8M".
                uint64_t bytes = 0;
                if (auto number = utils::to_double(*size_s)) {
                    bytes = static_cast<uint64_t>(*number);
                    if (size_s->find('K') != std::string::npos) bytes *= 1024;
                    else if (size_s->find('M') != std::string::npos) bytes *= 1024 * 1024;
                }
                if (bytes == 0) continue;

                if (*level == 1 && *type_s == "Data")         stats.cache_l1d_bytes = bytes;
                else if (*level == 1 && *type_s == "Instruction") stats.cache_l1i_bytes = bytes;
                else if (*level == 2) stats.cache_l2_bytes = bytes;
                else if (*level == 3) stats.cache_l3_bytes = bytes;
            }
        }
    }

#elif defined(SYSMON_MACOS)
    {
        char vendor[128] = {};
        size_t len = sizeof(vendor);
        if (sysctlbyname("machdep.cpu.vendor", vendor, &len, nullptr, 0) == 0 && vendor[0] != '\0') {
            stats.vendor = vendor;
        } else {
            // Apple Silicon has no machdep.cpu.vendor; infer from the architecture.
            stats.vendor = (stats.model.find("Apple") != std::string::npos) ? "Apple" : "";
        }
    }

    stats.cache_l1d_bytes = sysctl_u64("hw.l1dcachesize");
    stats.cache_l1i_bytes = sysctl_u64("hw.l1icachesize");
    stats.cache_l2_bytes  = sysctl_u64("hw.l2cachesize");
    stats.cache_l3_bytes  = sysctl_u64("hw.l3cachesize");
    stats.sockets         = static_cast<unsigned int>(sysctl_u64("hw.packages").value_or(1));

    // Hybrid core split, and the P/E label for every logical core.
    const auto p_cores = sysctl_u64("hw.perflevel0.logicalcpu");
    const auto e_cores = sysctl_u64("hw.perflevel1.logicalcpu");
    if (p_cores.has_value() && e_cores.has_value() && *e_cores > 0) {
        stats.performance_cores = static_cast<unsigned int>(*p_cores);
        stats.efficiency_cores  = static_cast<unsigned int>(*e_cores);
        for (size_t i = 0; i < stats.per_core.size(); ++i) {
            stats.per_core[i].cluster = (i < *p_cores) ? "P" : "E";
        }
    }
    if (stats.logical_cores > 0 && stats.physical_cores > 0) {
        stats.threads_per_core = stats.logical_cores / stats.physical_cores;
    }

    stats.thermal_pressure = thermal_pressure_level();

    // Instruction-set features, as reported by the kernel's feature flags.
    {
        char features[2048] = {};
        size_t len = sizeof(features);
        if (sysctlbyname("machdep.cpu.features", features, &len, nullptr, 0) == 0 &&
            features[0] != '\0') {
            stats.flags = utils::split_whitespace(features);
        } else {
            // Apple Silicon advertises features as individual hw.optional.* keys.
            static const char* const arm_features[] = {
                "hw.optional.arm.FEAT_FP16", "hw.optional.arm.FEAT_SHA512",
                "hw.optional.arm.FEAT_SHA3", "hw.optional.arm.FEAT_LSE",
                "hw.optional.arm.FEAT_DotProd", "hw.optional.arm.FEAT_BF16",
                "hw.optional.arm.FEAT_I8MM", "hw.optional.AdvSIMD",
                "hw.optional.armv8_crc32", "hw.optional.arm.FEAT_PAuth",
            };
            for (const char* key : arm_features) {
                int present = 0;
                size_t plen = sizeof(present);
                if (sysctlbyname(key, &present, &plen, nullptr, 0) == 0 && present != 0) {
                    std::string name(key);
                    const auto dot = name.rfind('.');
                    stats.flags.push_back(dot == std::string::npos ? name : name.substr(dot + 1));
                }
            }
        }
    }

#elif defined(SYSMON_WINDOWS)
    constexpr const char* kCpuKey = "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    stats.vendor = read_registry_string(kCpuKey, "VendorIdentifier").value_or("");

    if (auto mhz = read_registry_dword(kCpuKey, "~MHz")) {
        stats.base_frequency_mhz = static_cast<double>(*mhz);
    }

    // Cache sizes and socket count from the processor topology.
    {
        DWORD length = 0;
        GetLogicalProcessorInformationEx(RelationAll, nullptr, &length);
        if (length > 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            std::vector<char> buffer(length);
            if (GetLogicalProcessorInformationEx(
                    RelationAll,
                    reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
                    &length)) {
                unsigned int packages = 0;
                DWORD offset = 0;
                while (offset < length) {
                    auto* item = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                        buffer.data() + offset);
                    if (item->Size == 0) break;

                    if (item->Relationship == RelationProcessorPackage) {
                        ++packages;
                    } else if (item->Relationship == RelationCache) {
                        const auto& c = item->Cache;
                        const auto bytes = static_cast<uint64_t>(c.CacheSize);
                        if (c.Level == 1 && c.Type == CacheData)          stats.cache_l1d_bytes = bytes;
                        else if (c.Level == 1 && c.Type == CacheInstruction) stats.cache_l1i_bytes = bytes;
                        else if (c.Level == 2) stats.cache_l2_bytes = bytes;
                        else if (c.Level == 3) stats.cache_l3_bytes = bytes;
                    }
                    offset += item->Size;
                }
                if (packages > 0) stats.sockets = packages;
            }
        }
    }

    if (stats.logical_cores > 0 && stats.physical_cores > 0) {
        stats.threads_per_core = stats.logical_cores / stats.physical_cores;
    }
#endif
}

// ---------------------------------------------------------------------------
// Identity and frequency
// ---------------------------------------------------------------------------

std::optional<std::string> CpuMonitor::get_cpu_model() {
#if defined(SYSMON_LINUX)
    if (auto cpuinfo = utils::read_file("/proc/cpuinfo")) {
        std::istringstream iss(cpuinfo.value());
        std::string line;
        while (std::getline(iss, line)) {
            // x86 uses "model name"; arm64 boards often only have "Hardware".
            if (utils::starts_with(line, "model name") ||
                utils::starts_with(line, "Hardware")   ||
                utils::starts_with(line, "Processor")) {
                const auto pos = line.find(':');
                if (pos != std::string::npos) return utils::trim(line.substr(pos + 1));
            }
        }
    }
    return std::nullopt;

#elif defined(SYSMON_MACOS)
    char buf[256] = {};
    size_t len = sizeof(buf);
    if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) != 0) {
        return std::nullopt;
    }
    std::string model(buf);

    // Annotate the hybrid core split, which the brand string omits.
    const auto p_cores = sysctl_u64("hw.perflevel0.physicalcpu");
    const auto e_cores = sysctl_u64("hw.perflevel1.physicalcpu");
    if (p_cores.value_or(0) > 0 && e_cores.value_or(0) > 0) {
        model += " (" + std::to_string(*p_cores) + "P + " + std::to_string(*e_cores) + "E cores)";
    }
    return model;

#elif defined(SYSMON_WINDOWS)
    return read_registry_string("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                                "ProcessorNameString");
#else
    return std::nullopt;
#endif
}

std::optional<unsigned int> CpuMonitor::get_logical_cores() {
#if defined(SYSMON_LINUX)
    auto cpuinfo = utils::read_file("/proc/cpuinfo");
    if (!cpuinfo.has_value()) return std::nullopt;
    unsigned int count = 0;
    std::istringstream iss(cpuinfo.value());
    std::string line;
    while (std::getline(iss, line)) {
        if (utils::starts_with(line, "processor")) ++count;
    }
    return count > 0 ? std::optional<unsigned int>(count) : std::nullopt;

#elif defined(SYSMON_MACOS)
    const auto n = sysctl_u64("hw.logicalcpu");
    if (!n.has_value()) return std::nullopt;
    return static_cast<unsigned int>(*n);

#elif defined(SYSMON_WINDOWS)
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0
         ? std::optional<unsigned int>(static_cast<unsigned int>(si.dwNumberOfProcessors))
         : std::nullopt;
#else
    return std::nullopt;
#endif
}

std::optional<unsigned int> CpuMonitor::get_physical_cores() {
#if defined(SYSMON_LINUX)
    // "cpu cores" is per socket, so multiply by the number of physical ids.
    auto cpuinfo = utils::read_file("/proc/cpuinfo");
    if (!cpuinfo.has_value()) return std::nullopt;

    std::istringstream iss(cpuinfo.value());
    std::string line;
    std::vector<std::string> physical_ids;
    std::optional<long long> cores_per_socket;
    while (std::getline(iss, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key   = utils::trim(line.substr(0, colon));
        const std::string value = utils::trim(line.substr(colon + 1));
        if (key == "cpu cores" && !cores_per_socket.has_value()) {
            cores_per_socket = utils::to_int(value);
        }
        if (key == "physical id" &&
            std::find(physical_ids.begin(), physical_ids.end(), value) == physical_ids.end()) {
            physical_ids.push_back(value);
        }
    }
    if (cores_per_socket.has_value() && *cores_per_socket > 0) {
        const auto sockets = physical_ids.empty() ? 1u : static_cast<unsigned int>(physical_ids.size());
        return static_cast<unsigned int>(*cores_per_socket) * sockets;
    }
    return std::nullopt;

#elif defined(SYSMON_MACOS)
    const auto n = sysctl_u64("hw.physicalcpu");
    if (!n.has_value()) return std::nullopt;
    return static_cast<unsigned int>(*n);

#elif defined(SYSMON_WINDOWS)
    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
    if (length == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER) return std::nullopt;

    std::vector<char> buffer(length);
    if (!GetLogicalProcessorInformationEx(
            RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
            &length)) {
        return std::nullopt;
    }
    unsigned int cores = 0;
    DWORD offset = 0;
    while (offset < length) {
        auto* item = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
        if (item->Size == 0) break;
        if (item->Relationship == RelationProcessorCore) ++cores;
        offset += item->Size;
    }
    return cores > 0 ? std::optional<unsigned int>(cores) : std::nullopt;
#else
    return std::nullopt;
#endif
}

std::optional<double> CpuMonitor::get_cpu_frequency() {
#if defined(SYSMON_LINUX)
    // Average the per-core scaling frequencies: on a big.LITTLE or boosting CPU
    // cpu0 alone is not representative of the package.
    double sum = 0.0;
    unsigned int n = 0;
    for (unsigned int i = 0; i < 512; ++i) {
        const std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(i) +
                                 "/cpufreq/scaling_cur_freq";
        auto line = utils::read_first_line(path);
        if (!line.has_value()) break;
        if (auto khz = utils::to_double(*line)) {
            sum += khz.value() / 1000.0;
            ++n;
        }
    }
    if (n > 0) return sum / n;

    if (auto cpuinfo = utils::read_file("/proc/cpuinfo")) {
        std::istringstream iss(cpuinfo.value());
        std::string line;
        while (std::getline(iss, line)) {
            if (utils::starts_with(line, "cpu MHz")) {
                const auto pos = line.find(':');
                if (pos != std::string::npos) return utils::to_double(line.substr(pos + 1));
            }
        }
    }
    return std::nullopt;

#elif defined(SYSMON_MACOS)
    // Intel Macs expose hw.cpufrequency.  Apple Silicon does not publish a
    // current frequency through any unprivileged API, so this stays empty and
    // the UI reports N/A rather than inventing a number.
    if (auto freq = sysctl_u64("hw.cpufrequency")) {
        if (*freq > 0) return static_cast<double>(*freq) / 1e6;
    }
    return std::nullopt;

#elif defined(SYSMON_WINDOWS)
    // CallNtPowerInformation reports the live per-core frequency; average it.
    struct ProcessorPowerInfo {
        ULONG Number;
        ULONG MaxMhz;
        ULONG CurrentMhz;
        ULONG MhzLimit;
        ULONG MaxIdleState;
        ULONG CurrentIdleState;
    };
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const ULONG count = si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1;
    std::vector<ProcessorPowerInfo> info(count);
    if (CallNtPowerInformation(ProcessorInformation, nullptr, 0, info.data(),
                               static_cast<ULONG>(info.size() * sizeof(ProcessorPowerInfo))) == 0) {
        double sum = 0.0;
        for (const auto& p : info) sum += static_cast<double>(p.CurrentMhz);
        if (!info.empty()) return sum / static_cast<double>(info.size());
    }
    return std::nullopt;
#else
    return std::nullopt;
#endif
}

std::optional<double> CpuMonitor::get_max_frequency() {
#if defined(SYSMON_LINUX)
    if (auto line = utils::read_first_line("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq")) {
        if (auto khz = utils::to_double(*line)) return khz.value() / 1000.0;
    }
    return std::nullopt;

#elif defined(SYSMON_MACOS)
    if (auto freq = sysctl_u64("hw.cpufrequency_max")) {
        if (*freq > 0) return static_cast<double>(*freq) / 1e6;
    }
    return std::nullopt;

#elif defined(SYSMON_WINDOWS)
    struct ProcessorPowerInfo {
        ULONG Number;
        ULONG MaxMhz;
        ULONG CurrentMhz;
        ULONG MhzLimit;
        ULONG MaxIdleState;
        ULONG CurrentIdleState;
    };
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const ULONG count = si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1;
    std::vector<ProcessorPowerInfo> info(count);
    if (CallNtPowerInformation(ProcessorInformation, nullptr, 0, info.data(),
                               static_cast<ULONG>(info.size() * sizeof(ProcessorPowerInfo))) == 0 &&
        !info.empty() && info[0].MaxMhz > 0) {
        return static_cast<double>(info[0].MaxMhz);
    }
    return std::nullopt;
#else
    return std::nullopt;
#endif
}
