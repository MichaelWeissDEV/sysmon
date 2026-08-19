/**
 * @file network_monitor.hpp
 * @brief Network interface statistics monitor.
 */

#ifndef SYSMON_NETWORK_MONITOR_HPP
#define SYSMON_NETWORK_MONITOR_HPP

#include "sysmon/stats.hpp"
#include <vector>
#include <string>
#include <map>
#include <chrono>
#include <cstdint>
#include <optional>

/**
 * @brief Collects network interface statistics.
 *
 * Reads per-interface byte/packet counters and computes per-second
 * throughput rates using two successive measurements.
 */
class NetworkMonitor {
public:
    NetworkMonitor();

    /**
     * @brief Collect network statistics for all active interfaces.
     * @return Vector of NetworkStats, one per interface.
     */
    std::vector<NetworkStats> read();

private:
    struct IfaceSnapshot {
        uint64_t rx_bytes{0};
        uint64_t tx_bytes{0};
        uint64_t rx_packets{0};
        uint64_t tx_packets{0};
        std::chrono::steady_clock::time_point timestamp;
    };

    std::map<std::string, IfaceSnapshot> previous_snapshots_;

    // Platform-specific implementations
    std::vector<NetworkStats> read_linux();
    std::vector<NetworkStats> read_macos();

    // Helpers
    std::string get_ip_address(const std::string& iface);
    std::string get_ip6_address(const std::string& iface);
    std::optional<uint64_t> get_link_speed(const std::string& iface);
    bool        is_interface_up(const std::string& iface);
};

#endif // SYSMON_NETWORK_MONITOR_HPP
