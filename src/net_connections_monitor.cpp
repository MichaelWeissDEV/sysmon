#include "sysmon/net_connections_monitor.hpp"
#include "sysmon/utils.hpp"
#include "sysmon/platform.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <filesystem>
#include <cstdio>
#include <memory>
#include <array>

#if defined(SYSMON_POSIX)
#  include <arpa/inet.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers for IP/Port decoding
// ---------------------------------------------------------------------------

std::string NetConnectionsMonitor::hex_to_ip4(const std::string& hex) {
    if (hex.length() != 8) return hex;
    try {
        uint32_t ip_num = static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
        struct in_addr in;
        in.s_addr = ip_num;
        char buf[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &in, buf, sizeof(buf))) {
            return std::string(buf);
        }
    } catch (...) {}
    return hex;
}

std::string NetConnectionsMonitor::hex_to_ip6(const std::string& hex) {
    if (hex.length() != 32) return hex;
    try {
        struct in6_addr in6;
        for (int i = 0; i < 4; ++i) {
            std::string part = hex.substr(static_cast<size_t>(i * 8), 8);
            uint32_t val = static_cast<uint32_t>(std::stoul(part, nullptr, 16));
            reinterpret_cast<uint32_t*>(in6.s6_addr)[i] = val;
        }
        char buf[INET6_ADDRSTRLEN];
        if (inet_ntop(AF_INET6, &in6, buf, sizeof(buf))) {
            return std::string(buf);
        }
    } catch (...) {}
    return hex;
}

uint16_t NetConnectionsMonitor::hex_to_port(const std::string& hex) {
    try {
        return static_cast<uint16_t>(std::stoul(hex, nullptr, 16));
    } catch (...) {
        return 0;
    }
}

std::string NetConnectionsMonitor::tcp_state_name(int code) {
    switch (code) {
        case 1:  return "ESTABLISHED";
        case 2:  return "SYN_SENT";
        case 3:  return "SYN_RECV";
        case 4:  return "FIN_WAIT1";
        case 5:  return "FIN_WAIT2";
        case 6:  return "TIME_WAIT";
        case 7:  return "CLOSE";
        case 8:  return "CLOSE_WAIT";
        case 9:  return "LAST_ACK";
        case 10: return "LISTEN";
        case 11: return "CLOSING";
        default: return "UNKNOWN";
    }
}

void NetConnectionsMonitor::build_inode_map() {
    inode_pid_map_.clear();
    pid_name_map_.clear();

#if defined(SYSMON_LINUX)
    try {
        for (const auto& proc_entry : fs::directory_iterator("/proc")) {
            if (!proc_entry.is_directory()) continue;
            std::string pid_str = proc_entry.path().filename().string();
            if (pid_str.empty() || !std::isdigit(pid_str[0])) continue;

            int pid = 0;
            try { pid = std::stoi(pid_str); } catch (...) { continue; }

            auto comm = utils::read_file("/proc/" + pid_str + "/comm");
            if (comm.has_value()) {
                pid_name_map_[pid] = utils::trim(comm.value());
            }

            std::string fd_dir = "/proc/" + pid_str + "/fd";
            std::error_code ec;
            if (!fs::exists(fd_dir, ec)) continue;

            for (const auto& fd_entry : fs::directory_iterator(fd_dir, ec)) {
                if (ec) break;
                std::string target = fs::read_symlink(fd_entry.path(), ec).string();
                if (ec) continue;

                if (target.rfind("socket:[", 0) == 0 && target.back() == ']') {
                    std::string inode_str = target.substr(8, target.length() - 9);
                    try {
                        uint64_t inode = std::stoull(inode_str);
                        inode_pid_map_[inode] = pid;
                    } catch (...) {}
                }
            }
        }
    } catch (...) {}
#endif
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<NetConnectionStats> NetConnectionsMonitor::read(bool include_listen, unsigned int limit) {
#if defined(SYSMON_LINUX)
    return read_linux(include_listen, limit);
#elif defined(SYSMON_MACOS)
    return read_macos(include_listen, limit);
#else
    (void)include_listen; (void)limit;
    return {};
#endif
}

// ---------------------------------------------------------------------------
// Linux implementation
// ---------------------------------------------------------------------------

#if defined(SYSMON_LINUX)

std::vector<NetConnectionStats> NetConnectionsMonitor::read_linux(bool include_listen, unsigned int limit) {
    build_inode_map();

    std::vector<NetConnectionStats> result;

    auto parse_file = [&](const std::string& path, const std::string& proto, bool is_ipv6) {
        auto content = utils::read_file(path);
        if (!content.has_value()) return;

        std::istringstream iss(content.value());
        std::string line;
        std::getline(iss, line); // Skip header line

        while (std::getline(iss, line)) {
            auto tokens = utils::split(line, ' ');
            tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                         [](const std::string& s) { return s.empty(); }), tokens.end());

            if (tokens.size() < 10) continue;

            auto local_parts = utils::split(tokens[1], ':');
            auto rem_parts   = utils::split(tokens[2], ':');
            if (local_parts.size() != 2 || rem_parts.size() != 2) continue;

            int state_code = 0;
            try { state_code = std::stoi(tokens[3], nullptr, 16); } catch (...) {}
            std::string state = (proto.find("UDP") != std::string::npos) ? "UNCONN" : tcp_state_name(state_code);

            if (!include_listen && (state == "LISTEN" || state == "UNCONN" || state == "CLOSE")) {
                continue;
            }

            NetConnectionStats conn;
            conn.protocol = proto;
            conn.local_addr  = is_ipv6 ? hex_to_ip6(local_parts[0]) : hex_to_ip4(local_parts[0]);
            conn.local_port  = hex_to_port(local_parts[1]);
            conn.remote_addr = is_ipv6 ? hex_to_ip6(rem_parts[0])   : hex_to_ip4(rem_parts[0]);
            conn.remote_port = hex_to_port(rem_parts[1]);
            conn.state       = state;

            try {
                uint64_t inode = std::stoull(tokens[9]);
                auto it = inode_pid_map_.find(inode);
                if (it != inode_pid_map_.end()) {
                    conn.pid = it->second;
                    auto name_it = pid_name_map_.find(conn.pid);
                    if (name_it != pid_name_map_.end()) {
                        conn.process_name = name_it->second;
                    }
                }
            } catch (...) {}

            result.push_back(conn);
        }
    };

    parse_file("/proc/net/tcp", "TCP", false);
    parse_file("/proc/net/tcp6", "TCP6", true);
    parse_file("/proc/net/udp", "UDP", false);
    parse_file("/proc/net/udp6", "UDP6", true);

    std::sort(result.begin(), result.end(), [](const NetConnectionStats& a, const NetConnectionStats& b) {
        if (a.state != b.state) {
            if (a.state == "ESTABLISHED") return true;
            if (b.state == "ESTABLISHED") return false;
            if (a.state == "LISTEN") return true;
            if (b.state == "LISTEN") return false;
        }
        return a.local_port < b.local_port;
    });

    if (limit > 0 && result.size() > limit) {
        result.resize(limit);
    }

    return result;
}

#else
std::vector<NetConnectionStats> NetConnectionsMonitor::read_linux(bool, unsigned int) { return {}; }
#endif

// ---------------------------------------------------------------------------
// macOS implementation using netstat -anv
// ---------------------------------------------------------------------------

#if defined(SYSMON_MACOS)

std::vector<NetConnectionStats> NetConnectionsMonitor::read_macos(bool include_listen, unsigned int limit) {
    std::vector<NetConnectionStats> result;

    auto run_netstat = [](const char* proto) -> std::string {
        // Fixed command; never built from user input.
        std::string cmd = std::string("netstat -anv -p ") + proto + " 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return {};
        std::string out;
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            out += buffer;
        }
        pclose(pipe);
        return out;
    };

    std::string tcp_out = run_netstat("tcp");
    std::string udp_out = run_netstat("udp");

    auto tcp = parse_macos_netstat_tcp(tcp_out, include_listen);
    auto udp = parse_macos_netstat_udp(udp_out, include_listen);

    result.reserve(tcp.size() + udp.size());
    result.insert(result.end(), tcp.begin(), tcp.end());
    result.insert(result.end(), udp.begin(), udp.end());

    std::sort(result.begin(), result.end(), [](const NetConnectionStats& a, const NetConnectionStats& b) {
        if (a.state != b.state) {
            if (a.state == "ESTABLISHED") return true;
            if (b.state == "ESTABLISHED") return false;
            if (a.state == "LISTEN") return true;
            if (b.state == "LISTEN") return false;
        }
        return a.local_port < b.local_port;
    });

    if (limit > 0 && result.size() > limit) {
        result.resize(limit);
    }

    return result;
}

// Parses the `process:pid` token that may span multiple whitespace tokens
// when the process name contains spaces (e.g. "Code Helper (Plu:14454").
static void parse_macos_process_token(const std::vector<std::string>& tokens,
                                      size_t start,
                                      NetConnectionStats& conn) {
    for (size_t i = start; i < tokens.size(); ++i) {
        auto colon = tokens[i].rfind(':');
        if (colon == std::string::npos || colon + 1 >= tokens[i].size()) continue;

        std::string pid_part = tokens[i].substr(colon + 1);
        bool all_digits = !pid_part.empty() &&
                          std::all_of(pid_part.begin(), pid_part.end(),
                                      [](unsigned char c) { return std::isdigit(c) != 0; });
        if (!all_digits) continue;

        try {
            conn.pid = std::stoi(pid_part);
        } catch (...) {
            break;
        }

        std::string name;
        for (size_t p = start; p < i; ++p) {
            if (!name.empty()) name += " ";
            name += tokens[p];
        }
        if (!name.empty()) name += " ";
        name += tokens[i].substr(0, colon);
        conn.process_name = name;
        return;
    }
}

void NetConnectionsMonitor::parse_address_port(const std::string& addrport,
                                               std::string& addr, uint16_t& port) {
    addr  = addrport;
    port  = 0;
    auto pos = addrport.rfind('.');
    if (pos == std::string::npos) return;
    addr = addrport.substr(0, pos);
    try {
        port = static_cast<uint16_t>(std::stoi(addrport.substr(pos + 1)));
    } catch (...) {
        addr = addrport;
        port = 0;
    }
}

std::vector<NetConnectionStats> NetConnectionsMonitor::parse_macos_netstat_tcp(const std::string& output,
                                                                               bool include_listen) {
    std::vector<NetConnectionStats> result;
    std::istringstream iss(output);
    std::string line;
    bool header_passed = false;

    // netstat -anv -p tcp columns (whitespace-split):
    // 0 Proto  1 Recv-Q  2 Send-Q  3 Local Address  4 Foreign Address  5 (state)
    // 6 rxbytes  7 txbytes  8 rhiwat  9 shiwat  10+ process:pid  11+ state/options/...
    while (std::getline(iss, line)) {
        if (line.find("Proto") != std::string::npos || line.find("Active") != std::string::npos) {
            header_passed = true;
            continue;
        }
        if (!header_passed) continue;

        auto tokens = utils::split(line, ' ');
        tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                     [](const std::string& s) { return s.empty(); }), tokens.end());
        if (tokens.size() < 6) continue;

        std::string proto  = tokens[0];
        std::string state  = tokens[5];
        if (!include_listen && (state == "LISTEN" || state == "CLOSED")) continue;

        NetConnectionStats conn;
        conn.protocol = proto;
        conn.state    = state;
        parse_address_port(tokens[3], conn.local_addr, conn.local_port);
        parse_address_port(tokens[4], conn.remote_addr, conn.remote_port);

        if (tokens.size() > 7) {
            try { conn.rx_bytes = std::stoull(tokens[6]); } catch (...) {}
            try { conn.tx_bytes = std::stoull(tokens[7]); } catch (...) {}
        }

        // process:pid starts after shiwat (index 9).  If a malformed or
        // unexpected line is encountered, PID/process remain N/A.
        if (tokens.size() > 10) {
            parse_macos_process_token(tokens, 10, conn);
        }

        result.push_back(conn);
    }

    return result;
}

std::vector<NetConnectionStats> NetConnectionsMonitor::parse_macos_netstat_udp(const std::string& output,
                                                                               bool include_listen) {
    std::vector<NetConnectionStats> result;
    std::istringstream iss(output);
    std::string line;
    bool header_passed = false;

    // netstat -anv -p udp columns (whitespace-split): there is no state column.
    // 0 Proto  1 Recv-Q  2 Send-Q  3 Local Address  4 Foreign Address
    // 5 rxbytes  6 txbytes  7 rhiwat  8 shiwat  9+ process:pid  10+ state/options/...
    while (std::getline(iss, line)) {
        if (line.find("Proto") != std::string::npos || line.find("Active") != std::string::npos) {
            header_passed = true;
            continue;
        }
        if (!header_passed) continue;

        auto tokens = utils::split(line, ' ');
        tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                     [](const std::string& s) { return s.empty(); }), tokens.end());
        if (tokens.size() < 5) continue;

        NetConnectionStats conn;
        conn.protocol = tokens[0];
        conn.state    = "UNCONN";
        if (!include_listen) continue;

        parse_address_port(tokens[3], conn.local_addr, conn.local_port);
        parse_address_port(tokens[4], conn.remote_addr, conn.remote_port);

        if (tokens.size() > 7) {
            try { conn.rx_bytes = std::stoull(tokens[5]); } catch (...) {}
            try { conn.tx_bytes = std::stoull(tokens[6]); } catch (...) {}
        }

        // process:pid starts after shiwat (index 8).
        if (tokens.size() > 9) {
            parse_macos_process_token(tokens, 9, conn);
        }

        result.push_back(conn);
    }

    return result;
}

#else
std::vector<NetConnectionStats> NetConnectionsMonitor::read_macos(bool, unsigned int) { return {}; }
#endif
