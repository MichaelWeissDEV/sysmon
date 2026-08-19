/**
 * @file net_connections_monitor.hpp
 * @brief Network connections monitor (TCP/UDP per-process).
 */

#ifndef SYSMON_NET_CONNECTIONS_MONITOR_HPP
#define SYSMON_NET_CONNECTIONS_MONITOR_HPP

#include "sysmon/stats.hpp"
#include "sysmon/platform.hpp"
#include <vector>
#include <map>
#include <string>

/**
 * @brief Reads active TCP/UDP connections and associates them with processes.
 *
 * On Linux: reads /proc/net/tcp, /proc/net/tcp6, /proc/net/udp, /proc/net/udp6
 *           and matches inodes to processes via /proc/<pid>/fd/.
 * On macOS: parses netstat output.
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
    // inode → pid cache
    std::map<uint64_t, int> inode_pid_map_;
    std::map<int, std::string> pid_name_map_;

    void build_inode_map();

    std::vector<NetConnectionStats> read_linux(bool include_listen, unsigned int limit);
    std::vector<NetConnectionStats> read_macos(bool include_listen, unsigned int limit);
};

#endif // SYSMON_NET_CONNECTIONS_MONITOR_HPP
