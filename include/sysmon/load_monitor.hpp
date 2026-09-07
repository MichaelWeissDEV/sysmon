/**
 * @file load_monitor.hpp
 * @brief System load average and process state counts.
 */

#ifndef SYSMON_LOAD_MONITOR_HPP
#define SYSMON_LOAD_MONITOR_HPP

#include "sysmon/stats.hpp"

/**
 * @brief Reads load averages and running/sleeping/zombie process counts.
 *
 * Windows has no load average; on that platform the load fields stay zero and
 * only the process state counts are populated.
 */
class LoadMonitor {
public:
    /**
     * @brief Read current load statistics.
     */
    LoadStats read();
};

#endif // SYSMON_LOAD_MONITOR_HPP
