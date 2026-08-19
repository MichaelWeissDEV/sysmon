#include "sysmon/network_monitor.hpp"
#include "sysmon/utils.hpp"
#include "sysmon/platform.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>

#if defined(SYSMON_POSIX)
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/ioctl.h>
#  include <net/if.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <ifaddrs.h>
#  include <unistd.h>
#  include <cstring>
#endif

#if defined(SYSMON_LINUX)
#  include <net/if.h>
#endif

#if defined(SYSMON_MACOS)
#  include <net/if_media.h>
#endif

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

NetworkMonitor::NetworkMonitor() = default;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<NetworkStats> NetworkMonitor::read() {
#if defined(SYSMON_LINUX)
    return read_linux();
#elif defined(SYSMON_MACOS)
    return read_macos();
#else
    return {};
#endif
}

// ---------------------------------------------------------------------------
// Linux: /proc/net/dev
// ---------------------------------------------------------------------------

#if defined(SYSMON_LINUX)

std::vector<NetworkStats> NetworkMonitor::read_linux() {
    auto net_dev = utils::read_file("/proc/net/dev");
    if (!net_dev.has_value()) return {};

    auto now = std::chrono::steady_clock::now();
    std::vector<NetworkStats> result;
    std::istringstream iss(net_dev.value());
    std::string line;

    // Skip header lines
    std::getline(iss, line);
    std::getline(iss, line);

    while (std::getline(iss, line)) {
        // Format: "  eth0: rx_bytes rx_packets rx_errs ... tx_bytes tx_packets ..."
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string iface = utils::trim(line.substr(0, colon));
        std::string data  = line.substr(colon + 1);
        auto parts = utils::split(data, ' ');
        // Filter empty tokens
        parts.erase(std::remove_if(parts.begin(), parts.end(),
                    [](const std::string& s){ return s.empty(); }), parts.end());

        if (parts.size() < 16) continue;

        uint64_t rx_bytes   = 0, rx_packets = 0, rx_errors = 0;
        uint64_t tx_bytes   = 0, tx_packets = 0, tx_errors = 0;
        try {
            rx_bytes   = std::stoull(parts[0]);
            rx_packets = std::stoull(parts[1]);
            rx_errors  = std::stoull(parts[2]);
            tx_bytes   = std::stoull(parts[8]);
            tx_packets = std::stoull(parts[9]);
            tx_errors  = std::stoull(parts[10]);
        } catch (...) { continue; }

        NetworkStats ns;
        ns.interface       = iface;
        ns.rx_bytes_total  = rx_bytes;
        ns.tx_bytes_total  = tx_bytes;
        ns.rx_packets_total = rx_packets;
        ns.tx_packets_total = tx_packets;
        ns.rx_errors       = rx_errors;
        ns.tx_errors       = tx_errors;
        ns.is_up           = is_interface_up(iface);
        ns.ip_address      = get_ip_address(iface);
        ns.ip6_address     = get_ip6_address(iface);
        ns.speed_mbps      = get_link_speed(iface);

        // Compute per-second rates
        auto it = previous_snapshots_.find(iface);
        if (it != previous_snapshots_.end()) {
            double dt = std::chrono::duration<double>(now - it->second.timestamp).count();
            if (dt > 0.0) {
                if (rx_bytes >= it->second.rx_bytes) {
                    ns.rx_bytes_per_sec = static_cast<double>(rx_bytes - it->second.rx_bytes) / dt;
                }
                if (tx_bytes >= it->second.tx_bytes) {
                    ns.tx_bytes_per_sec = static_cast<double>(tx_bytes - it->second.tx_bytes) / dt;
                }
            }
        }

        // Update snapshot
        IfaceSnapshot snap;
        snap.rx_bytes   = rx_bytes;
        snap.tx_bytes   = tx_bytes;
        snap.rx_packets = rx_packets;
        snap.tx_packets = tx_packets;
        snap.timestamp  = now;
        previous_snapshots_[iface] = snap;

        result.push_back(ns);
    }

    // Sort: physical NICs first, then loopback
    std::stable_sort(result.begin(), result.end(), [](const NetworkStats& a, const NetworkStats& b) {
        bool a_lo = (a.interface == "lo");
        bool b_lo = (b.interface == "lo");
        if (a_lo != b_lo) return !a_lo;
        return a.interface < b.interface;
    });

    return result;
}

#else
std::vector<NetworkStats> NetworkMonitor::read_linux() { return {}; }
#endif

// ---------------------------------------------------------------------------
// macOS: getifaddrs
// ---------------------------------------------------------------------------

#if defined(SYSMON_MACOS)

std::vector<NetworkStats> NetworkMonitor::read_macos() {
    auto now = std::chrono::steady_clock::now();
    std::vector<NetworkStats> result;

    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) != 0) return result;

    std::map<std::string, NetworkStats> iface_map;

    for (struct ifaddrs* ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_name == nullptr) continue;
        std::string iface = ifa->ifa_name;

        auto& ns = iface_map[iface];
        ns.interface = iface;
        ns.is_up = (ifa->ifa_flags & IFF_UP) != 0;

        if (ifa->ifa_addr == nullptr) continue;

        if (ifa->ifa_addr->sa_family == AF_INET) {
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET,
                      &reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr)->sin_addr,
                      buf, sizeof(buf));
            ns.ip_address = buf;
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            char buf[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6,
                      &reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_addr)->sin6_addr,
                      buf, sizeof(buf));
            ns.ip6_address = buf;
        }

#ifdef AF_LINK
        if (ifa->ifa_addr->sa_family == AF_LINK && ifa->ifa_data != nullptr) {
            struct if_data* ifd = reinterpret_cast<struct if_data*>(ifa->ifa_data);
            ns.rx_bytes_total   = ifd->ifi_ibytes;
            ns.tx_bytes_total   = ifd->ifi_obytes;
            ns.rx_packets_total = ifd->ifi_ipackets;
            ns.tx_packets_total = ifd->ifi_opackets;
            ns.rx_errors        = ifd->ifi_ierrors;
            ns.tx_errors        = ifd->ifi_oerrors;
        }
#endif
    }

    freeifaddrs(ifap);

    for (auto& [iface, ns] : iface_map) {
        // Compute rates
        auto it = previous_snapshots_.find(iface);
        if (it != previous_snapshots_.end()) {
            double dt = std::chrono::duration<double>(now - it->second.timestamp).count();
            if (dt > 0.0) {
                if (ns.rx_bytes_total >= it->second.rx_bytes) {
                    ns.rx_bytes_per_sec = static_cast<double>(ns.rx_bytes_total - it->second.rx_bytes) / dt;
                }
                if (ns.tx_bytes_total >= it->second.tx_bytes) {
                    ns.tx_bytes_per_sec = static_cast<double>(ns.tx_bytes_total - it->second.tx_bytes) / dt;
                }
            }
        }

        IfaceSnapshot snap;
        snap.rx_bytes  = ns.rx_bytes_total;
        snap.tx_bytes  = ns.tx_bytes_total;
        snap.timestamp = now;
        previous_snapshots_[iface] = snap;

        // On macOS, skip dormant virtual interfaces like anpi, ap, awdl, gif, stf, utun if they have no IP and no traffic
        bool is_virtual_dormant = (iface.rfind("anpi", 0) == 0 ||
                                   iface.rfind("ap", 0) == 0 ||
                                   iface.rfind("awdl", 0) == 0 ||
                                   iface.rfind("gif", 0) == 0 ||
                                   iface.rfind("stf", 0) == 0 ||
                                   iface.rfind("llw", 0) == 0 ||
                                   iface.rfind("utun", 0) == 0 ||
                                   iface.rfind("bridge", 0) == 0);
        if (is_virtual_dormant && ns.ip_address.empty() && ns.rx_bytes_total == 0 && ns.tx_bytes_total == 0) {
            continue;
        }

        result.push_back(ns);
    }

    // Sort
    std::stable_sort(result.begin(), result.end(), [](const NetworkStats& a, const NetworkStats& b) {
        bool a_lo = (a.interface == "lo0" || a.interface == "lo");
        bool b_lo = (b.interface == "lo0" || b.interface == "lo");
        if (a_lo != b_lo) return !a_lo;
        return a.interface < b.interface;
    });

    return result;
}

#else
std::vector<NetworkStats> NetworkMonitor::read_macos() { return {}; }
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string NetworkMonitor::get_ip_address(const std::string& iface) {
#if defined(SYSMON_POSIX)
    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) != 0) return "";

    std::string result;
    for (struct ifaddrs* ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_name == nullptr || ifa->ifa_addr == nullptr) continue;
        if (iface != ifa->ifa_name) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET,
                      &reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr)->sin_addr,
                      buf, sizeof(buf));
            result = buf;
            break;
        }
    }
    freeifaddrs(ifap);
    return result;
#else
    (void)iface;
    return "";
#endif
}

std::string NetworkMonitor::get_ip6_address(const std::string& iface) {
#if defined(SYSMON_POSIX)
    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) != 0) return "";

    std::string result;
    for (struct ifaddrs* ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_name == nullptr || ifa->ifa_addr == nullptr) continue;
        if (iface != ifa->ifa_name) continue;
        if (ifa->ifa_addr->sa_family == AF_INET6) {
            char buf[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6,
                      &reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_addr)->sin6_addr,
                      buf, sizeof(buf));
            result = buf;
            break;
        }
    }
    freeifaddrs(ifap);
    return result;
#else
    (void)iface;
    return "";
#endif
}

std::optional<uint64_t> NetworkMonitor::get_link_speed(const std::string& iface) {
#if defined(SYSMON_LINUX)
    auto speed_file = utils::read_file("/sys/class/net/" + iface + "/speed");
    if (speed_file.has_value()) {
        try {
            long long spd = std::stoll(speed_file.value());
            if (spd > 0) return static_cast<uint64_t>(spd);
        } catch (...) {}
    }
#else
    // No reliable unprivileged link-speed source on macOS; report N/A
    // instead of a fabricated value.
    (void)iface;
#endif
    return std::nullopt;
}

bool NetworkMonitor::is_interface_up(const std::string& iface) {
#if defined(SYSMON_POSIX)
    auto operstate = utils::read_file("/sys/class/net/" + iface + "/operstate");
    if (operstate.has_value()) {
        return utils::trim(operstate.value()) == "up";
    }
    // Fallback: SIOCGIFFLAGS
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
    bool up = false;
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0) {
        up = (ifr.ifr_flags & IFF_UP) != 0;
    }
    close(fd);
    return up;
#else
    (void)iface;
    return false;
#endif
}
