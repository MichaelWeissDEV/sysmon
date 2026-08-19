/**
 * @file disk_monitor.hpp
 * @brief Filesystem usage (statvfs) monitor.
 */

#ifndef SYSMON_DISK_MONITOR_HPP
#define SYSMON_DISK_MONITOR_HPP

#include "sysmon/stats.hpp"
#include <vector>
#include <string>

/**
 * @brief Reads disk / filesystem usage statistics.
 */
class DiskMonitor {
public:
    /**
     * @brief Read all mounted filesystems (excluding virtual).
     */
    std::vector<DiskStats> read();

private:
    bool is_virtual_filesystem(const std::string& fs_type);
};

#endif // SYSMON_DISK_MONITOR_HPP