/**
 * @file net_connections_monitor.hpp
 * @brief Network connections monitor (TCP/UDP per-process).
 */

#ifndef SYSMON_NET_CONNECTIONS_MONITOR_HPP
#define SYSMON_NET_CONNECTIONS_MONITOR_HPP

#include "sysmon/stats.hpp"
#include "sysmon/platform.hpp"
#include <chrono>
#include <string>
#include <vector>
#include <map>
#include <string>

/**
 * @brief Reads active TCP/UDP connections and associates them with processes.
 *
 * On Linux: reads /proc/net/tcp, /proc/net/tcp6, /proc/net/udp, /proc/net/udp6
 *           and matches inodes to processes via /proc/<pid>/fd/.
 * On macOS: parses netstat output.
 * On Windows: GetExtendedTcpTable / GetExtendedUdpTable, which already carry
 *             the owning PID.
 */
class NetConnectionsMonitor {
public:
    NetConnectionsMonitor() = default;

    /**
     * @brief Read all active connections.
     * @param include_listen  Include LISTEN / UNCONN sockets.
     * @param limit           Max connections to return (0 = all).
     * @return Vector of NetConnectionStats sorted by state.
     */
    std::vector<NetConnectionStats> read(bool include_listen = false,
                                          unsigned int limit = 100);

    /**
     * @brief Summarise a connection list into per-state counters.
     *
     * The socket census belongs to whichever component already walked the
     * socket tables, which is this one.
     */
    static void summarize(const std::vector<NetConnectionStats>& conns, NetGlobalStats& out);

    /**
     * @brief Attribute network throughput to the processes that caused it.
     *
     * macOS `netstat -anv` reports cumulative rxbytes/txbytes per socket next
     * to the owning process, all of it unprivileged, so summing per PID and
     * differencing two samples gives a real per-process rate.  Linux's
     * /proc/net/tcp carries no byte counters and the Windows EStats API needs
     * administrator rights, so on those platforms the rates stay empty rather
     * than becoming a zero that reads as "this process used no network".
     *
     * Rates are computed against the previous call, so this has to be called
     * once per refresh with the *complete* connection list — a list truncated
     * for display would attribute only part of the traffic.
     *
     * @param conns Every connection of this sample, before any display limit.
     * @param procs Process list to annotate in place.
     */
    void attribute_bandwidth(const std::vector<NetConnectionStats>& conns,
                             std::vector<ProcessStats>& procs);

    /** @brief True when this platform can attribute bandwidth to a process. */
    static constexpr bool bandwidth_attribution_supported() {
#if defined(SYSMON_MACOS)
        return true;
#else
        return false;
#endif
    }

    // Pure parsers for `netstat -anv -p tcp` / `netstat -anv -p udp` output.
    // These are platform-independent so they can be exercised with fixtures.
    static std::vector<NetConnectionStats> parse_macos_netstat_tcp(const std::string& output,
                                                                   bool include_listen);
    static std::vector<NetConnectionStats> parse_macos_netstat_udp(const std::string& output,
                                                                   bool include_listen);

    // Helpers
    static std::string hex_to_ip4(const std::string& hex);
    static std::string hex_to_ip6(const std::string& hex);
    static uint16_t    hex_to_port(const std::string& hex);
    static std::string tcp_state_name(int code);
    static void        parse_address_port(const std::string& addrport,
                                          std::string& addr, uint16_t& port);

private:
    /// Cumulative byte counters of one socket in the previous sample.
    struct SocketTraffic {
        uint64_t rx{0};
        uint64_t tx{0};
        int      pid{-1};
    };

    // inode → pid cache
    std::map<uint64_t, int> inode_pid_map_;
    std::map<int, std::string> pid_name_map_;

    /// Keyed by socket identity, not by PID.
    ///
    /// A per-PID total jumps by a whole socket's lifetime counters the moment
    /// that process opens a connection, which turns one refresh interval into
    /// a rate of gigabytes per second.  Differencing each socket against
    /// itself and only counting sockets present in both samples is what makes
    /// the number mean throughput.
    std::map<std::string, SocketTraffic> previous_traffic_;
    std::chrono::steady_clock::time_point previous_traffic_time_{};
    bool have_previous_traffic_{false};

    /// Stable identity of a socket across samples.
    static std::string socket_key(const NetConnectionStats& conn);

    void build_inode_map();

    std::vector<NetConnectionStats> read_linux(bool include_listen, unsigned int limit);
    std::vector<NetConnectionStats> read_macos(bool include_listen, unsigned int limit);
    std::vector<NetConnectionStats> read_windows(bool include_listen, unsigned int limit);
};

#endif // SYSMON_NET_CONNECTIONS_MONITOR_HPP
