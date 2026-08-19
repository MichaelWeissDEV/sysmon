/**
 * @file memory_monitor.hpp
 * @brief RAM and swap statistics monitor.
 */

#ifndef SYSMON_MEMORY_MONITOR_HPP
#define SYSMON_MEMORY_MONITOR_HPP

#include "sysmon/stats.hpp"
#include <optional>
#include <string>
#include <cstdint>

/**
 * @brief Reads memory and swap statistics.
 */
class MemoryMonitor {
public:
    /**
     * @brief Read current memory statistics.
     */
    MemoryStats read();

    /**
     * @brief Parse a single "Key:    1234 kB" entry from /proc/meminfo data.
     * @return Value in bytes, or nullopt if the key is missing/invalid.
     */
    static std::optional<uint64_t> parse_meminfo_value(const std::string& key,
                                                       const std::string& data);

private:
};

#endif // SYSMON_MEMORY_MONITOR_HPP