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

    // Use netstat -anv -p tcp (fast, numeric only, includes process:pid)
    FILE* pipe = popen("netstat -anv -p tcp 2>/dev/null", "r");
    if (!pipe) return result;

    char buffer[1024];
    bool header_passed = false;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        if (line.find("Proto") != std::string::npos || line.find("Active") != std::string::npos) {
            header_passed = true;
            continue;
        }
        if (!header_passed) continue;

        auto tokens = utils::split(line, ' ');
        tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                     [](const std::string& s) { return s.empty(); }), tokens.end());

        // Typical tokens:
        // [0] Proto (tcp4/tcp6)
        // [1] Recv-Q
        // [2] Send-Q
        // [3] Local Address (e.g. 192.168.0.4.50162)
        // [4] Foreign Address (e.g. 18.97.36.61.443)
        // [5] (state) (e.g. ESTABLISHED, LISTEN, SYN_SENT)
        // [6] rxbytes
        // [7] txbytes
        // [8] rhiwat
        // [9] shiwat
        // [10] process:pid (or if process name has spaces, remaining tokens before options)
        if (tokens.size() < 6) continue;

        std::string proto = tokens[0];
        std::string local = tokens[3];
        std::string foreign = tokens[4];
        std::string state = tokens[5];

        if (!include_listen && (state == "LISTEN" || state == "CLOSED")) continue;

        auto lpos = local.rfind('.');
        std::string laddr = (lpos != std::string::npos) ? local.substr(0, lpos) : local;
        uint16_t lport = 0;
        if (lpos != std::string::npos) {
            try { lport = static_cast<uint16_t>(std::stoi(local.substr(lpos + 1))); } catch (...) {}
        }

        auto rpos = foreign.rfind('.');
        std::string raddr = (rpos != std::string::npos) ? foreign.substr(0, rpos) : foreign;
        uint16_t rport = 0;
        if (rpos != std::string::npos) {
            try { rport = static_cast<uint16_t>(std::stoi(foreign.substr(rpos + 1))); } catch (...) {}
        }

        NetConnectionStats conn;
        conn.protocol = proto;
        conn.local_addr = laddr;
        conn.local_port = lport;
        conn.remote_addr = raddr;
        conn.remote_port = rport;
        conn.state = state;

        if (tokens.size() > 7) {
            try { conn.rx_bytes = std::stoull(tokens[6]); } catch (...) {}
            try { conn.tx_bytes = std::stoull(tokens[7]); } catch (...) {}
        }

        // Look for process:pid token (contains colon ':')
        for (size_t i = 8; i < tokens.size(); ++i) {
            auto colon = tokens[i].rfind(':');
            if (colon != std::string::npos && colon + 1 < tokens[i].size()) {
                std::string pid_part = tokens[i].substr(colon + 1);
                if (std::isdigit(pid_part[0])) {
                    try {
                        conn.pid = std::stoi(pid_part);
                        // The process name might span tokens from 10 up to here
                        std::string pname;
                        for (size_t p = 10; p < i; ++p) {
                            if (!pname.empty()) pname += " ";
                            pname += tokens[p];
                        }
                        if (!pname.empty()) {
                            pname += " " + tokens[i].substr(0, colon);
                        } else {
                            pname = tokens[i].substr(0, colon);
                        }
                        conn.process_name = pname;
                    } catch (...) {}
                    break;
                }
            }
        }

        result.push_back(conn);
    }
    pclose(pipe);

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
std::vector<NetConnectionStats> NetConnectionsMonitor::read_macos(bool, unsigned int) { return {}; }
#endif
