#include "sysmon/load_monitor.hpp"
#include "sysmon/utils.hpp"
#include "sysmon/platform.hpp"

#include <fstream>
#include <sstream>

#if defined(SYSMON_MACOS)
#  include <sys/sysctl.h>
#endif

LoadStats LoadMonitor::read() {
    LoadStats stats;

#if defined(SYSMON_LINUX)
    // Load averages from /proc/loadavg
    auto loadavg = utils::read_file("/proc/loadavg");
    if (loadavg.has_value()) {
        std::string line = loadavg.value();
        // Format: "1.00 0.50 0.25 1/200 12345"
        auto parts = utils::split(line, ' ');
        if (parts.size() >= 3) {
            try {
                stats.load_1min  = std::stod(parts[0]);
                stats.load_5min  = std::stod(parts[1]);
                stats.load_15min = std::stod(parts[2]);
            } catch (...) {}
        }
        // "running/total" in part 3
        if (parts.size() >= 4) {
            auto counts = utils::split(parts[3], '/');
            if (counts.size() == 2) {
                try {
                    stats.running_processes = static_cast<unsigned int>(std::stoi(counts[0]));
                    stats.total_processes   = static_cast<unsigned int>(std::stoi(counts[1]));
                } catch (...) {}
            }
        }
    }

#elif defined(SYSMON_MACOS)
    double load[3] = {0, 0, 0};
    if (getloadavg(load, 3) == 3) {
        stats.load_1min  = load[0];
        stats.load_5min  = load[1];
        stats.load_15min = load[2];
    }

    // Process count via sysctl
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
    size_t size = 0;
    if (sysctl(mib, 4, nullptr, &size, nullptr, 0) == 0) {
        stats.total_processes = static_cast<unsigned int>(size / sizeof(struct kinfo_proc));
    }
#endif

    return stats;
}