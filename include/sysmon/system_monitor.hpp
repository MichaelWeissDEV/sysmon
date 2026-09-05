/**
 * @file system_monitor.hpp
 * @brief General system information (hostname, OS, kernel, uptime, arch).
 */

#ifndef SYSMON_SYSTEM_MONITOR_HPP
#define SYSMON_SYSTEM_MONITOR_HPP

#include "sysmon/stats.hpp"
#include <string>

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

    /**
     * @brief Parse a pretty OS name out of /etc/os-release content.
     *
     * Prefers PRETTY_NAME, falls back to NAME + VERSION.  Handles quoted and
     * unquoted values.  Returns an empty string when nothing usable is found.
     */
    static std::string parse_os_release(const std::string& content);

private:
    std::string get_os_name();
    std::string get_architecture();
};

#endif // SYSMON_SYSTEM_MONITOR_HPP
