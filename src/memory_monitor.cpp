#include "sysmon/memory_monitor.hpp"
#include "sysmon/platform.hpp"
#include "sysmon/utils.hpp"

#include <sstream>
#include <string>

#if defined(SYSMON_MACOS)
#  include <sys/types.h>
#  include <sys/sysctl.h>
#  include <mach/mach.h>
#  include <mach/vm_statistics.h>
#  include <mach/mach_types.h>
#  include <mach/mach_init.h>
#  include <mach/mach_host.h>
#endif

#if defined(SYSMON_WINDOWS)
#  include <windows.h>
#  include <psapi.h>
#endif

MemoryMonitor::MemoryMonitor() = default;

MemoryStats MemoryMonitor::read() {
    MemoryStats stats;
    PagingCounters paging;

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = first_read_
        ? 0.0
        : std::chrono::duration<double>(now - prev_ts_).count();

#if defined(SYSMON_LINUX)
    auto meminfo = utils::read_file("/proc/meminfo");
    if (!meminfo.has_value()) return stats;

    auto parse = [&](const std::string& key) -> uint64_t {
        return parse_meminfo_value(key, meminfo.value()).value_or(0);
    };
    auto parse_opt = [&](const std::string& key) -> std::optional<uint64_t> {
        return parse_meminfo_value(key, meminfo.value());
    };

    stats.ram_total_bytes     = parse("MemTotal");
    stats.ram_available_bytes = parse("MemAvailable");
    stats.ram_free_bytes      = parse("MemFree");
    stats.ram_cached_bytes    = parse("Cached");
    stats.ram_buffer_bytes    = parse("Buffers");
    stats.swap_total_bytes    = parse("SwapTotal");

    stats.active_bytes     = parse_opt("Active");
    stats.inactive_bytes   = parse_opt("Inactive");
    stats.wired_bytes      = parse_opt("Unevictable");
    stats.shared_bytes     = parse_opt("Shmem");
    stats.slab_bytes       = parse_opt("Slab");
    stats.dirty_bytes      = parse_opt("Dirty");
    stats.commit_total_bytes = parse_opt("Committed_AS");
    stats.commit_limit_bytes = parse_opt("CommitLimit");

    const uint64_t swap_free = parse("SwapFree");
    stats.swap_used_bytes = (stats.swap_total_bytes > swap_free)
                            ? stats.swap_total_bytes - swap_free : 0;

    if (stats.ram_total_bytes > 0) {
        stats.ram_used_bytes = stats.ram_total_bytes > stats.ram_available_bytes
                             ? stats.ram_total_bytes - stats.ram_available_bytes : 0;
        stats.ram_usage_percent =
            static_cast<double>(stats.ram_used_bytes) /
            static_cast<double>(stats.ram_total_bytes) * 100.0;
    }
    if (stats.swap_total_bytes > 0) {
        stats.swap_usage_percent =
            static_cast<double>(stats.swap_used_bytes) /
            static_cast<double>(stats.swap_total_bytes) * 100.0;
    }

    // Paging counters live in /proc/vmstat.
    if (auto vmstat = utils::read_file("/proc/vmstat")) {
        std::istringstream iss(vmstat.value());
        std::string line;
        while (std::getline(iss, line)) {
            const auto parts = utils::split_whitespace(line);
            if (parts.size() < 2) continue;
            const auto value = utils::to_int(parts[1]);
            if (!value.has_value() || *value < 0) continue;
            const auto v = static_cast<uint64_t>(*value);

            if      (parts[0] == "pgfault")  paging.faults       = v;
            else if (parts[0] == "pgmajfault") paging.major_faults = v;
            else if (parts[0] == "pgpgin")   paging.page_ins     = v;
            else if (parts[0] == "pgpgout")  paging.page_outs    = v;
            else if (parts[0] == "pswpin")   paging.swap_ins     = v;
            else if (parts[0] == "pswpout")  paging.swap_outs    = v;
        }
    }

    // The kernel's own memory-pressure metric, when psi is compiled in.
    if (auto psi = utils::read_file("/proc/pressure/memory")) {
        std::istringstream iss(psi.value());
        std::string line;
        while (std::getline(iss, line)) {
            if (!utils::starts_with(line, "some")) continue;
            for (const auto& token : utils::split_whitespace(line)) {
                if (utils::starts_with(token, "avg10=")) {
                    stats.pressure_percent = utils::to_double(token.substr(6));
                }
            }
        }
    }

#elif defined(SYSMON_MACOS)
    {
        int mib[2] = {CTL_HW, HW_MEMSIZE};
        uint64_t total = 0;
        size_t len = sizeof(total);
        if (sysctl(mib, 2, &total, &len, nullptr, 0) == 0) {
            stats.ram_total_bytes = total;
        }
    }

    vm_size_t page_size = 0;
    host_page_size(mach_host_self(), &page_size);

    vm_statistics64_data_t vm_stat{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm_stat), &count) == KERN_SUCCESS) {
        const auto ps = static_cast<uint64_t>(page_size);
        const uint64_t free_mem     = static_cast<uint64_t>(vm_stat.free_count)            * ps;
        const uint64_t active_mem   = static_cast<uint64_t>(vm_stat.active_count)          * ps;
        const uint64_t inactive_mem = static_cast<uint64_t>(vm_stat.inactive_count)        * ps;
        const uint64_t wired_mem    = static_cast<uint64_t>(vm_stat.wire_count)            * ps;
        const uint64_t compressed   = static_cast<uint64_t>(vm_stat.compressor_page_count) * ps;
        const uint64_t speculative  = static_cast<uint64_t>(vm_stat.speculative_count)     * ps;
        const uint64_t purgeable    = static_cast<uint64_t>(vm_stat.purgeable_count)       * ps;
        const uint64_t external     = static_cast<uint64_t>(vm_stat.external_page_count)   * ps;

        // Matches Activity Monitor's "Memory Used": anonymous pages that are
        // resident plus wired plus what the compressor holds.
        stats.ram_used_bytes      = active_mem + wired_mem + compressed;
        stats.ram_free_bytes      = free_mem;
        stats.ram_available_bytes = free_mem + inactive_mem + speculative + purgeable;
        stats.ram_cached_bytes    = external;

        stats.active_bytes     = active_mem;
        stats.inactive_bytes   = inactive_mem;
        stats.wired_bytes      = wired_mem;
        stats.compressed_bytes = compressed;

        paging.faults       = static_cast<uint64_t>(vm_stat.faults);
        paging.page_ins     = static_cast<uint64_t>(vm_stat.pageins);
        paging.page_outs    = static_cast<uint64_t>(vm_stat.pageouts);
        paging.swap_ins     = static_cast<uint64_t>(vm_stat.swapins);
        paging.swap_outs    = static_cast<uint64_t>(vm_stat.swapouts);
    }

    if (stats.ram_total_bytes > 0) {
        stats.ram_usage_percent =
            static_cast<double>(stats.ram_used_bytes) /
            static_cast<double>(stats.ram_total_bytes) * 100.0;
    }

    {
        struct xsw_usage swap{};
        size_t swap_len = sizeof(swap);
        if (sysctlbyname("vm.swapusage", &swap, &swap_len, nullptr, 0) == 0) {
            stats.swap_total_bytes = swap.xsu_total;
            stats.swap_used_bytes  = swap.xsu_used;
            if (stats.swap_total_bytes > 0) {
                stats.swap_usage_percent =
                    static_cast<double>(stats.swap_used_bytes) /
                    static_cast<double>(stats.swap_total_bytes) * 100.0;
            }
        }
    }

    // The kernel's own memory pressure level, as used by Activity Monitor.
    {
        int level = 0;
        size_t len = sizeof(level);
        if (sysctlbyname("kern.memorystatus_vm_pressure_level", &level, &len, nullptr, 0) == 0) {
            // 1 = normal, 2 = warning, 4 = critical.
            stats.pressure_percent = (level >= 4) ? 100.0 : (level >= 2 ? 50.0 : 0.0);
        }
    }

#elif defined(SYSMON_WINDOWS)
    {
        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms)) {
            stats.ram_total_bytes     = ms.ullTotalPhys;
            stats.ram_available_bytes = ms.ullAvailPhys;
            stats.ram_free_bytes      = ms.ullAvailPhys;
            stats.ram_used_bytes      = ms.ullTotalPhys > ms.ullAvailPhys
                                      ? ms.ullTotalPhys - ms.ullAvailPhys : 0;
            stats.ram_usage_percent   = static_cast<double>(ms.dwMemoryLoad);

            // Windows reports a combined physical+pagefile commit limit; the
            // swap portion is what is left once physical memory is subtracted.
            stats.commit_limit_bytes = ms.ullTotalPageFile;
            stats.commit_total_bytes = ms.ullTotalPageFile > ms.ullAvailPageFile
                                     ? ms.ullTotalPageFile - ms.ullAvailPageFile : 0;

            const uint64_t swap_total = ms.ullTotalPageFile > ms.ullTotalPhys
                                      ? ms.ullTotalPageFile - ms.ullTotalPhys : 0;
            const uint64_t swap_avail = ms.ullAvailPageFile > ms.ullAvailPhys
                                      ? ms.ullAvailPageFile - ms.ullAvailPhys : 0;
            stats.swap_total_bytes = swap_total;
            stats.swap_used_bytes  = swap_total > swap_avail ? swap_total - swap_avail : 0;
            if (swap_total > 0) {
                stats.swap_usage_percent =
                    static_cast<double>(stats.swap_used_bytes) /
                    static_cast<double>(swap_total) * 100.0;
            }
        }
    }

    {
        PERFORMANCE_INFORMATION pi{};
        pi.cb = sizeof(pi);
        if (GetPerformanceInfo(&pi, sizeof(pi))) {
            const auto page = static_cast<uint64_t>(pi.PageSize);
            stats.ram_cached_bytes = static_cast<uint64_t>(pi.SystemCache) * page;
            stats.slab_bytes       = static_cast<uint64_t>(pi.KernelPaged + pi.KernelNonpaged) * page;
        }
    }
#endif

    fill_paging_rates(stats, paging, elapsed);

    prev_paging_ = paging;
    prev_ts_     = now;
    first_read_  = false;

    return stats;
}

void MemoryMonitor::fill_paging_rates(MemoryStats& stats, const PagingCounters& current,
                                      double elapsed_seconds) {
    if (elapsed_seconds <= 1e-6) return;

    auto rate = [elapsed_seconds](const std::optional<uint64_t>& now,
                                  const std::optional<uint64_t>& before) -> std::optional<double> {
        if (!now.has_value() || !before.has_value()) return std::nullopt;
        if (*now < *before) return std::nullopt;   // counter reset
        return static_cast<double>(*now - *before) / elapsed_seconds;
    };

    stats.page_faults_per_sec  = rate(current.faults,       prev_paging_.faults);
    stats.major_faults_per_sec = rate(current.major_faults, prev_paging_.major_faults);
    stats.page_ins_per_sec     = rate(current.page_ins,     prev_paging_.page_ins);
    stats.page_outs_per_sec    = rate(current.page_outs,    prev_paging_.page_outs);
    stats.swap_ins_per_sec     = rate(current.swap_ins,     prev_paging_.swap_ins);
    stats.swap_outs_per_sec    = rate(current.swap_outs,    prev_paging_.swap_outs);
}

std::optional<uint64_t> MemoryMonitor::parse_meminfo_value(const std::string& key,
                                                           const std::string& data) {
    std::istringstream iss(data);
    std::string line;
    while (std::getline(iss, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        // Compare the whole field name, so "Active" does not match "Active(anon)".
        if (line.compare(0, colon, key) != 0) continue;

        std::string val = utils::trim(line.substr(colon + 1));
        const auto kb = val.find(" kB");
        if (kb != std::string::npos) val = val.substr(0, kb);
        if (auto v = utils::to_int(val)) {
            if (*v < 0) return std::nullopt;
            return static_cast<uint64_t>(*v) * 1024;
        }
        return std::nullopt;
    }
    return std::nullopt;
}
