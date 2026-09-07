/**
 * @file network_monitor.hpp
 * @brief Network interface statistics monitor.
 */

#ifndef SYSMON_NETWORK_MONITOR_HPP
#define SYSMON_NETWORK_MONITOR_HPP

#include "sysmon/stats.hpp"
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

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

    /**
     * @brief Collect host-wide networking facts (gateway, DNS servers).
     *
     * Socket state counts are filled in separately by NetConnectionsMonitor,
     * which is the component that already enumerates sockets.
     */
    NetGlobalStats read_global();

    /** @brief Extract the IPv4 default gateway from /proc/net/route content. */
    static std::string parse_proc_net_route(const std::string& content);

    /** @brief Extract nameserver entries from resolv.conf content. */
    static std::vector<std::string> parse_resolv_conf(const std::string& content);

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
    std::vector<NetworkStats> read_windows();

    /// Turn cumulative counters into per-second rates using the previous sample.
    void apply_rates(std::vector<NetworkStats>& interfaces,
                     std::chrono::steady_clock::time_point now);

    /// Fill in addresses, MAC, MTU and flags from getifaddrs() (POSIX only).
    void fill_addresses_posix(std::vector<NetworkStats>& interfaces);
};

#endif // SYSMON_NETWORK_MONITOR_HPP
