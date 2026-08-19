/**
 * @file load_monitor.hpp
 * @brief System load average and process count monitor.
 */

#ifndef SYSMON_LOAD_MONITOR_HPP
#define SYSMON_LOAD_MONITOR_HPP

#include "sysmon/stats.hpp"

/**
 * @brief Reads load averages and running/total process counts.
 */
class LoadMonitor {
public:
    /**
     * @brief Read current load statistics.
     */
    LoadStats read();
};

#endif // SYSMON_LOAD_MONITOR_HPP