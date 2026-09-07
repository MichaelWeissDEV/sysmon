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
 * - Linux:   /proc/diskstats
 * - macOS:   IOKit IOBlockStorageDriver statistics
 * - Windows: IOCTL_DISK_PERFORMANCE on each \\.\PhysicalDriveN
 *
 * All figures are deltas between two successive calls, so the first call
 * reports zero rates rather than an average since boot.
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
        uint64_t read_time_ns{0};    ///< Cumulative time spent reading
        uint64_t write_time_ns{0};   ///< Cumulative time spent writing
        uint64_t busy_time_ms{0};    ///< Cumulative device-busy time
        std::chrono::steady_clock::time_point timestamp;
    };

    std::map<std::string, DeviceSnapshot> previous_;

    std::vector<DiskIOStats> read_linux();
    std::vector<DiskIOStats> read_macos();
    std::vector<DiskIOStats> read_windows();

    bool is_physical_device(const std::string& dev);
};

#endif // SYSMON_DISK_IO_MONITOR_HPP
