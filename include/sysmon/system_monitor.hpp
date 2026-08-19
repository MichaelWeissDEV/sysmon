/**
 * @file system_monitor.hpp
 * @brief General system information (hostname, OS, kernel, uptime, arch).
 */

#ifndef SYSMON_SYSTEM_MONITOR_HPP
#define SYSMON_SYSTEM_MONITOR_HPP

#include "sysmon/stats.hpp"

/**
 * @brief Reads general system metadata.
 */
class SystemMonitor {
public:
    /**
     * @brief Collect system information.
     * @return Populated SystemStats struct.
     */
    SystemStats read();

private:
    std::string get_os_name();
    std::string get_architecture();
};

#endif // SYSMON_SYSTEM_MONITOR_HPP