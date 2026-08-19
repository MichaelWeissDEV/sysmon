#include "sysmon/cpu_monitor.hpp"
#include "sysmon/temperature_monitor.hpp"
#include "sysmon/utils.hpp"
#include "sysmon/platform.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <chrono>

#if defined(SYSMON_MACOS)
#  include <sys/sysctl.h>
#  include <mach/mach.h>
#  include <mach/processor_info.h>
#  include <mach/mach_host.h>
#endif

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

CpuMonitor::CpuMonitor() = default;

CpuStats CpuMonitor::read() {
    CpuStats stats;

    stats.model          = get_cpu_model().value_or("unknown");
    stats.logical_cores  = get_logical_cores().value_or(0);
    stats.physical_cores = get_physical_cores().value_or(0);
    stats.max_frequency_mhz = get_max_frequency();

    auto now = std::chrono::steady_clock::now();

#if defined(SYSMON_LINUX)
    auto cur_times = read_proc_stat_linux();
#elif defined(SYSMON_MACOS)
    auto cur_times = read_cpu_times_macos();
#else
    std::vector<CpuTimes> cur_times;
#endif

    if (!first_read_ && !cur_times.empty() && !prev_times_.empty()) {
        // Aggregate totals: sum all cores
        CpuTimes agg_cur{}, agg_prev{};
        for (size_t i = 0; i < cur_times.size(); ++i) {
            agg_cur.user    += cur_times[i].user;
            agg_cur.nice    += cur_times[i].nice;
            agg_cur.system  += cur_times[i].system;
            agg_cur.idle    += cur_times[i].idle;
            agg_cur.iowait  += cur_times[i].iowait;
            agg_cur.irq     += cur_times[i].irq;
            agg_cur.softirq += cur_times[i].softirq;
            agg_cur.steal   += cur_times[i].steal;
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

        double user_pct = 0, sys_pct = 0, iowait_pct = 0, idle_pct = 0;
        stats.usage_percent = usage_from_delta(agg_prev, agg_cur,
                                               &user_pct, &sys_pct, &iowait_pct, &idle_pct);
        stats.user_percent   = user_pct;
        stats.system_percent = sys_pct;
        stats.iowait_percent = iowait_pct;
        stats.idle_percent   = idle_pct;

        // Per-core
        stats.per_core.resize(cur_times.size());
        for (size_t i = 0; i < cur_times.size(); ++i) {
            stats.per_core[i].id = static_cast<unsigned int>(i);
            if (i < prev_times_.size()) {
                stats.per_core[i].usage_percent =
                    usage_from_delta(prev_times_[i], cur_times[i]);
            }
        }
    }

    prev_times_ = cur_times;
    prev_ts_    = now;
    first_read_ = false;

    // Frequency
    stats.frequency_mhz = get_cpu_frequency();

    // Per-core frequencies (best effort)
#if defined(SYSMON_LINUX)
    for (size_t i = 0; i < stats.per_core.size(); ++i) {
        std::string freq_path = "/sys/devices/system/cpu/cpu" +
                                std::to_string(i) +
                                "/cpufreq/scaling_cur_freq";
        auto f = utils::read_file(freq_path);
        if (f.has_value()) {
            try {
                stats.per_core[i].frequency_mhz = std::stod(f.value()) / 1000.0;
            } catch (...) {}
        }
    }
#elif defined(SYSMON_MACOS)
    for (auto& core : stats.per_core) {
        core.frequency_mhz = stats.frequency_mhz;
    }
#endif

    // Temperature
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
        // Lines like "cpu0 ..." "cpu1 ..." (per-core)
        if (line.substr(0, 3) == "cpu" && line.size() > 3 && std::isdigit(line[3])) {
            auto parts = utils::split(line, ' ');
            if (parts.size() < 5) continue;
            CpuTimes t;
            try {
                t.user    = std::stoull(parts[1]);
                t.nice    = std::stoull(parts[2]);
                t.system  = std::stoull(parts[3]);
                t.idle    = std::stoull(parts[4]);
                if (parts.size() > 5)  t.iowait  = std::stoull(parts[5]);
                if (parts.size() > 6)  t.irq     = std::stoull(parts[6]);
                if (parts.size() > 7)  t.softirq = std::stoull(parts[7]);
                if (parts.size() > 8)  t.steal   = std::stoull(parts[8]);
            } catch (...) { continue; }
            result.push_back(t);
        }
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

    kern_return_t kr = host_processor_info(mach_host_self(),
                                           PROCESSOR_CPU_LOAD_INFO,
                                           &cpu_count,
                                           &cpu_info,
                                           &info_count);
    if (kr != KERN_SUCCESS) return {};

    std::vector<CpuTimes> result;
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
// Common helpers
// ---------------------------------------------------------------------------

double CpuMonitor::usage_from_delta(const CpuTimes& a, const CpuTimes& b,
                                     double* user_pct, double* sys_pct,
                                     double* iowait_pct, double* idle_pct) {
    auto total_a = a.user + a.nice + a.system + a.idle + a.iowait + a.irq + a.softirq + a.steal;
    auto total_b = b.user + b.nice + b.system + b.idle + b.iowait + b.irq + b.softirq + b.steal;

    // Counter reset / wrap between samples: treat as no useful delta.
    if (total_b < total_a) {
        if (user_pct)   *user_pct   = 0;
        if (sys_pct)    *sys_pct    = 0;
        if (iowait_pct) *iowait_pct = 0;
        if (idle_pct)   *idle_pct   = 100.0;
        return 0.0;
    }

    auto delta_total = static_cast<double>(total_b) - static_cast<double>(total_a);
    if (delta_total <= 0) {
        if (user_pct)   *user_pct   = 0;
        if (sys_pct)    *sys_pct    = 0;
        if (iowait_pct) *iowait_pct = 0;
        if (idle_pct)   *idle_pct   = 100.0;
        return 0.0;
    }

    double delta_idle   = static_cast<double>(b.idle)   - static_cast<double>(a.idle);
    double delta_user   = static_cast<double>(b.user + b.nice) - static_cast<double>(a.user + a.nice);
    double delta_sys    = static_cast<double>(b.system + b.irq + b.softirq) -
                          static_cast<double>(a.system + a.irq + a.softirq);
    double delta_iowait = static_cast<double>(b.iowait) - static_cast<double>(a.iowait);

    auto clamp_pct = [](double v) {
        return std::max(0.0, std::min(100.0, v));
    };

    if (user_pct)   *user_pct   = clamp_pct(delta_user   / delta_total * 100.0);
    if (sys_pct)    *sys_pct    = clamp_pct(delta_sys    / delta_total * 100.0);
    if (iowait_pct) *iowait_pct = clamp_pct(delta_iowait / delta_total * 100.0);
    if (idle_pct)   *idle_pct   = clamp_pct(delta_idle   / delta_total * 100.0);

    double usage = 100.0 * (1.0 - delta_idle / delta_total);
    return std::round(clamp_pct(usage) * 10.0) / 10.0;
}

std::optional<std::string> CpuMonitor::get_cpu_model() {
#if defined(SYSMON_LINUX)
    auto cpuinfo = utils::read_file("/proc/cpuinfo");
    if (!cpuinfo.has_value()) return std::nullopt;
    std::istringstream iss(cpuinfo.value());
    std::string line;
    while (std::getline(iss, line)) {
        if (line.substr(0, 10) == "model name") {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                return utils::trim(line.substr(pos + 1));
            }
        }
    }
    return std::nullopt;
#elif defined(SYSMON_MACOS)
    char buf[256] = {};
    size_t len = sizeof(buf);
    if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0) {
        std::string model(buf);
        // Check for Apple Silicon Performance and Efficiency core counts
        int p_cores = 0, e_cores = 0;
        size_t plen = sizeof(p_cores), elen = sizeof(e_cores);
        sysctlbyname("hw.perflevel0.physicalcpu", &p_cores, &plen, nullptr, 0);
        sysctlbyname("hw.perflevel1.physicalcpu", &e_cores, &elen, nullptr, 0);
        if (p_cores > 0 || e_cores > 0) {
            model += " (" + std::to_string(p_cores) + "P + " + std::to_string(e_cores) + "E cores)";
        }
        return model;
    }
    return std::nullopt;
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
        if (line.substr(0, 9) == "processor") ++count;
    }
    return count;
#elif defined(SYSMON_MACOS)
    int n = 0;
    size_t len = sizeof(n);
    if (sysctlbyname("hw.logicalcpu", &n, &len, nullptr, 0) == 0) {
        return static_cast<unsigned int>(n);
    }
    return std::nullopt;
#else
    return std::nullopt;
#endif
}

std::optional<unsigned int> CpuMonitor::get_physical_cores() {
#if defined(SYSMON_LINUX)
    auto cpuinfo = utils::read_file("/proc/cpuinfo");
    if (!cpuinfo.has_value()) return std::nullopt;
    std::istringstream iss(cpuinfo.value());
    std::string line;
    while (std::getline(iss, line)) {
        if (line.substr(0, 9) == "cpu cores") {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                try { return static_cast<unsigned int>(std::stoi(utils::trim(line.substr(pos + 1)))); }
                catch (...) {}
            }
        }
    }
    return std::nullopt;
#elif defined(SYSMON_MACOS)
    int n = 0;
    size_t len = sizeof(n);
    if (sysctlbyname("hw.physicalcpu", &n, &len, nullptr, 0) == 0) {
        return static_cast<unsigned int>(n);
    }
    return std::nullopt;
#else
    return std::nullopt;
#endif
}

std::optional<double> CpuMonitor::get_cpu_frequency() {
#if defined(SYSMON_LINUX)
    // Try scaling_cur_freq for cpu0
    auto f = utils::read_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    if (f.has_value()) {
        try { return std::stod(f.value()) / 1000.0; } catch (...) {}
    }
    // Fall back to /proc/cpuinfo
    auto cpuinfo = utils::read_file("/proc/cpuinfo");
    if (cpuinfo.has_value()) {
        std::istringstream iss(cpuinfo.value());
        std::string line;
        while (std::getline(iss, line)) {
            if (line.substr(0, 7) == "cpu MHz") {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    try { return std::stod(utils::trim(line.substr(pos + 1))); } catch (...) {}
                }
            }
        }
    }
    return std::nullopt;
#elif defined(SYSMON_MACOS)
    // Only report a frequency when a real sysctl value is available.
    // Apple Silicon does not reliably expose a current CPU frequency;
    // in that case this returns nullopt and the UI shows N/A.
    uint64_t freq = 0;
    size_t len = sizeof(freq);
    if (sysctlbyname("hw.cpufrequency", &freq, &len, nullptr, 0) == 0 && freq > 0) {
        return static_cast<double>(freq) / 1e6;
    }
    if (sysctlbyname("hw.cpufrequency_max", &freq, &len, nullptr, 0) == 0 && freq > 0) {
        return static_cast<double>(freq) / 1e6;
    }
    if (sysctlbyname("hw.cpufrequency_min", &freq, &len, nullptr, 0) == 0 && freq > 0) {
        return static_cast<double>(freq) / 1e6;
    }
    return std::nullopt;
#else
    return std::nullopt;
#endif
}

std::optional<double> CpuMonitor::get_max_frequency() {
#if defined(SYSMON_LINUX)
    auto f = utils::read_file("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    if (f.has_value()) {
        try { return std::stod(f.value()) / 1000.0; } catch (...) {}
    }
    return std::nullopt;
#elif defined(SYSMON_MACOS)
    uint64_t freq = 0;
    size_t len = sizeof(freq);
    if (sysctlbyname("hw.cpufrequency_max", &freq, &len, nullptr, 0) == 0 && freq > 0) {
        return static_cast<double>(freq) / 1e6;
    }
    return std::nullopt;
#else
    return std::nullopt;
#endif
}