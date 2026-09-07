/**
 * @file memory_monitor.hpp
 * @brief RAM and swap statistics monitor.
 */

#ifndef SYSMON_MEMORY_MONITOR_HPP
#define SYSMON_MEMORY_MONITOR_HPP

#include "sysmon/stats.hpp"
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

/**
 * @brief Reads memory and swap statistics.
 *
 * Paging rates are derived from two successive samples, so the first call
 * leaves them unset rather than reporting a rate since boot.
 */
class MemoryMonitor {
public:
    MemoryMonitor();

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
    /// Raw paging counters, used to turn cumulative totals into rates.
    struct PagingCounters {
        std::optional<uint64_t> faults;
        std::optional<uint64_t> major_faults;
        std::optional<uint64_t> page_ins;
        std::optional<uint64_t> page_outs;
        std::optional<uint64_t> swap_ins;
        std::optional<uint64_t> swap_outs;
    };

    PagingCounters prev_paging_{};
    std::chrono::steady_clock::time_point prev_ts_{};
    bool first_read_{true};

    void fill_paging_rates(MemoryStats& stats, const PagingCounters& current,
                           double elapsed_seconds);
};

#endif // SYSMON_MEMORY_MONITOR_HPP
