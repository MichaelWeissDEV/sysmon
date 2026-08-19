/**
 * @file disk_io_monitor.hpp
 * @brief Disk I/O throughput monitor.
 */

#ifndef SYSMON_DISK_IO_MONITOR_HPP
#define SYSMON_DISK_IO_MONITOR_HPP

#include "sysmon/stats.hpp"
#include <vector>
#include <string>
#include <map>
#include <chrono>
#include <cstdint>

/**
 * @brief Measures disk read/write throughput per block device.
 *
 * On Linux, reads /proc/diskstats and computes delta over time.
 * On macOS, uses IOKit disk statistics.
 */
class DiskIOMonitor {
public:
    DiskIOMonitor();

    /**
     * @brief Read disk I/O statistics for all physical block devices.
     * @return Vector of DiskIOStats, one per device.
     */
    std::vector<DiskIOStats> read();

private:
    struct DeviceSnapshot {
        uint64_t read_sectors{0};
        uint64_t write_sectors{0};
        uint64_t read_ios{0};
        uint64_t write_ios{0};
        uint64_t read_bytes{0};
        uint64_t write_bytes{0};
        std::chrono::steady_clock::time_point timestamp;
    };

    std::map<std::string, DeviceSnapshot> previous_;

    std::vector<DiskIOStats> read_linux();
    std::vector<DiskIOStats> read_macos();

    bool is_physical_device(const std::string& dev);
};

#endif // SYSMON_DISK_IO_MONITOR_HPP
