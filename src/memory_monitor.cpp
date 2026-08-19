#include "sysmon/memory_monitor.hpp"
#include "sysmon/utils.hpp"
#include "sysmon/platform.hpp"

#include <fstream>
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

MemoryStats MemoryMonitor::read() {
    MemoryStats stats;

#if defined(SYSMON_LINUX)
    auto meminfo = utils::read_file("/proc/meminfo");
    if (!meminfo.has_value()) return stats;

    auto parse = [&](const std::string& key) -> uint64_t {
        auto v = parse_meminfo_value(key, meminfo.value());
        return v.value_or(0);
    };

    stats.ram_total_bytes    = parse("MemTotal");
    stats.ram_available_bytes = parse("MemAvailable");
    stats.ram_cached_bytes   = parse("Cached");
    stats.ram_buffer_bytes   = parse("Buffers");
    stats.swap_total_bytes   = parse("SwapTotal");

    uint64_t swap_free = parse("SwapFree");
    stats.swap_used_bytes = (stats.swap_total_bytes > swap_free)
                            ? stats.swap_total_bytes - swap_free : 0;

    if (stats.ram_total_bytes > 0) {
        stats.ram_used_bytes = stats.ram_total_bytes - stats.ram_available_bytes;
        stats.ram_usage_percent =
            static_cast<double>(stats.ram_used_bytes) /
            static_cast<double>(stats.ram_total_bytes) * 100.0;
    }
    if (stats.swap_total_bytes > 0) {
        stats.swap_usage_percent =
            static_cast<double>(stats.swap_used_bytes) /
            static_cast<double>(stats.swap_total_bytes) * 100.0;
    }

#elif defined(SYSMON_MACOS)
    // Total physical RAM
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t total = 0;
    size_t len = sizeof(total);
    if (sysctl(mib, 2, &total, &len, nullptr, 0) == 0) {
        stats.ram_total_bytes = total;
    }

    // Page size
    vm_size_t page_size = 0;
    host_page_size(mach_host_self(), &page_size);

    // VM statistics
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm_stat),
                          &count) == KERN_SUCCESS) {
        uint64_t ps = static_cast<uint64_t>(page_size);
        uint64_t free_mem    = vm_stat.free_count            * ps;
        uint64_t active_mem  = vm_stat.active_count          * ps;
        uint64_t inactive_mem = vm_stat.inactive_count       * ps;
        uint64_t wired_mem   = vm_stat.wire_count            * ps;
        uint64_t compressed  = vm_stat.compressor_page_count * ps;

        stats.ram_used_bytes      = active_mem + wired_mem + compressed;
        stats.ram_available_bytes = free_mem + inactive_mem;
        stats.ram_cached_bytes    = inactive_mem;

        (void)active_mem; // suppress unused warning
    }

    if (stats.ram_total_bytes > 0) {
        stats.ram_usage_percent =
            static_cast<double>(stats.ram_used_bytes) /
            static_cast<double>(stats.ram_total_bytes) * 100.0;
    }

    // macOS swap (vm_stat doesn't expose it nicely; use sysctl)
    struct xsw_usage swap;
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
#endif

    return stats;
}

std::optional<uint64_t> MemoryMonitor::parse_meminfo_value(const std::string& key,
                                                             const std::string& data) {
    std::istringstream iss(data);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.substr(0, key.length()) == key) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string val = utils::trim(line.substr(pos + 1));
                // strip " kB"
                size_t kb = val.find(" kB");
                if (kb != std::string::npos) val = val.substr(0, kb);
                try { return static_cast<uint64_t>(std::stoll(val)) * 1024; }
                catch (...) {}
            }
        }
    }
    return std::nullopt;
}