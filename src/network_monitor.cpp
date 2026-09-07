#include "sysmon/network_monitor.hpp"
#include "sysmon/platform.hpp"
#include "sysmon/utils.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>
#include <sstream>

#if defined(SYSMON_POSIX)
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/ioctl.h>
#  include <net/if.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <ifaddrs.h>
#  include <unistd.h>
#endif

#if defined(SYSMON_LINUX)
#  include <linux/if_link.h>
#  include <filesystem>
#endif

#if defined(SYSMON_MACOS)
#  include <net/if_dl.h>
#  include <net/if_media.h>
#  include <net/if_types.h>
#endif

#if defined(SYSMON_WINDOWS)
#  include <windows.h>
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
#  include <netioapi.h>
#endif

namespace {

#if defined(SYSMON_WINDOWS)

/// Render a SOCKET_ADDRESS as a printable IP string.
std::string sockaddr_to_string(const SOCKET_ADDRESS& addr) {
    if (addr.lpSockaddr == nullptr) return "";
    char buffer[INET6_ADDRSTRLEN] = {};

    if (addr.lpSockaddr->sa_family == AF_INET) {
        auto* in4 = reinterpret_cast<sockaddr_in*>(addr.lpSockaddr);
        if (inet_ntop(AF_INET, &in4->sin_addr, buffer, sizeof(buffer)) != nullptr) return buffer;
    } else if (addr.lpSockaddr->sa_family == AF_INET6) {
        auto* in6 = reinterpret_cast<sockaddr_in6*>(addr.lpSockaddr);
        if (inet_ntop(AF_INET6, &in6->sin6_addr, buffer, sizeof(buffer)) != nullptr) return buffer;
    }
    return "";
}

/// Convert a wide adapter description to UTF-8.
std::string wide_to_utf8(const wchar_t* text) {
    if (text == nullptr) return "";
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return "";
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

/// Fetch the adapter list, growing the buffer until it fits.
std::vector<char> get_adapters_addresses() {
    ULONG size = 16 * 1024;
    std::vector<char> buffer;
    for (int attempt = 0; attempt < 4; ++attempt) {
        buffer.assign(size, 0);
        const ULONG rc = GetAdaptersAddresses(
            AF_UNSPEC,
            GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
            nullptr,
            reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()),
            &size);
        if (rc == NO_ERROR) return buffer;
        if (rc != ERROR_BUFFER_OVERFLOW) break;
    }
    return {};
}

#endif // SYSMON_WINDOWS

#if defined(SYSMON_MACOS)

/// Query SIOCGIFMEDIA for one interface; nullopt when the driver has no media
/// layer (tunnels, bridges) or the socket cannot be opened.
std::optional<struct ifmediareq> query_media_macos(const std::string& name) {
    const int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return std::nullopt;

    struct ifmediareq req{};
    std::strncpy(req.ifm_name, name.c_str(), sizeof(req.ifm_name) - 1);
    const int rc = ioctl(sock, SIOCGIFMEDIA, &req);
    close(sock);

    if (rc != 0) return std::nullopt;
    return req;
}

#endif // SYSMON_MACOS

/// Format a MAC address from raw bytes.
std::string format_mac(const unsigned char* bytes, size_t length) {
    if (bytes == nullptr || length == 0) return "";
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(length * 3);
    for (size_t i = 0; i < length; ++i) {
        if (i > 0) out += ':';
        out += hex[(bytes[i] >> 4) & 0xF];
        out += hex[bytes[i] & 0xF];
    }
    return out;
}

} // namespace

NetworkMonitor::NetworkMonitor() = default;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<NetworkStats> NetworkMonitor::read() {
#if defined(SYSMON_LINUX)
    auto interfaces = read_linux();
#elif defined(SYSMON_MACOS)
    auto interfaces = read_macos();
#elif defined(SYSMON_WINDOWS)
    auto interfaces = read_windows();
#else
    std::vector<NetworkStats> interfaces;
#endif

    apply_rates(interfaces, std::chrono::steady_clock::now());

    std::sort(interfaces.begin(), interfaces.end(),
              [](const NetworkStats& a, const NetworkStats& b) {
                  // Interfaces carrying traffic first, then by name.
                  const bool a_active = a.rx_bytes_total + a.tx_bytes_total > 0;
                  const bool b_active = b.rx_bytes_total + b.tx_bytes_total > 0;
                  if (a_active != b_active) return a_active;
                  if (a.is_up != b.is_up)   return a.is_up;
                  return a.name < b.name;
              });
    return interfaces;
}

void NetworkMonitor::apply_rates(std::vector<NetworkStats>& interfaces,
                                 std::chrono::steady_clock::time_point now) {
    for (auto& iface : interfaces) {
        const auto it = previous_snapshots_.find(iface.name);
        if (it != previous_snapshots_.end()) {
            const double dt = std::chrono::duration<double>(now - it->second.timestamp).count();
            if (dt > 0.0) {
                auto rate = [dt](uint64_t current, uint64_t before) {
                    // A counter that went backwards means a reset or a wrap:
                    // report zero rather than a spike.
                    return current >= before ? static_cast<double>(current - before) / dt : 0.0;
                };
                iface.rx_bytes_per_sec   = rate(iface.rx_bytes_total,   it->second.rx_bytes);
                iface.tx_bytes_per_sec   = rate(iface.tx_bytes_total,   it->second.tx_bytes);
                iface.rx_packets_per_sec = rate(iface.rx_packets_total, it->second.rx_packets);
                iface.tx_packets_per_sec = rate(iface.tx_packets_total, it->second.tx_packets);
            }
        }

        IfaceSnapshot snap;
        snap.rx_bytes   = iface.rx_bytes_total;
        snap.tx_bytes   = iface.tx_bytes_total;
        snap.rx_packets = iface.rx_packets_total;
        snap.tx_packets = iface.tx_packets_total;
        snap.timestamp  = now;
        previous_snapshots_[iface.name] = snap;
    }
}

// ---------------------------------------------------------------------------
// POSIX address / flag enrichment
// ---------------------------------------------------------------------------

#if defined(SYSMON_POSIX)

void NetworkMonitor::fill_addresses_posix(std::vector<NetworkStats>& interfaces) {
    struct ifaddrs* addrs = nullptr;
    if (getifaddrs(&addrs) != 0) return;

    for (struct ifaddrs* ifa = addrs; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_name == nullptr) continue;
        const std::string name = ifa->ifa_name;

        // Only annotate interfaces the counter pass already found, except on
        // macOS where this same walk is the source of the counters.
        auto existing = std::find_if(interfaces.begin(), interfaces.end(),
                                     [&name](const NetworkStats& n) { return n.name == name; });
        if (existing == interfaces.end()) continue;
        NetworkStats& iface = *existing;

#if !defined(SYSMON_LINUX)
        // On Linux the caller already set is_up from sysfs operstate, which is
        // more accurate than IFF_UP|IFF_RUNNING; do not overwrite it here.
        iface.is_up = (ifa->ifa_flags & IFF_UP) != 0 &&
                      (ifa->ifa_flags & IFF_RUNNING) != 0;
#endif
        iface.is_loopback = (ifa->ifa_flags & IFF_LOOPBACK) != 0;

        if (ifa->ifa_addr == nullptr) continue;

        if (ifa->ifa_addr->sa_family == AF_INET && iface.ip_address.empty()) {
            char buf[INET_ADDRSTRLEN] = {};
            auto* in4 = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
            if (inet_ntop(AF_INET, &in4->sin_addr, buf, sizeof(buf)) != nullptr) {
                iface.ip_address = buf;
            }
            if (ifa->ifa_netmask != nullptr) {
                auto* mask = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_netmask);
                char mbuf[INET_ADDRSTRLEN] = {};
                if (inet_ntop(AF_INET, &mask->sin_addr, mbuf, sizeof(mbuf)) != nullptr) {
                    iface.netmask = mbuf;
                }
            }
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            char buf[INET6_ADDRSTRLEN] = {};
            auto* in6 = reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_addr);
            if (inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof(buf)) != nullptr) {
                const std::string address = buf;
                const bool have_link_local = utils::starts_with(iface.ip6_address, "fe80");
                const bool is_link_local   = utils::starts_with(address, "fe80");
                // A routable address is more useful than a link-local one, and
                // interfaces list the link-local address first.
                if (iface.ip6_address.empty() || (have_link_local && !is_link_local)) {
                    iface.ip6_address = address;
                }
            }
        }
    }

    freeifaddrs(addrs);
}

#else

void NetworkMonitor::fill_addresses_posix(std::vector<NetworkStats>&) {}

#endif

// ---------------------------------------------------------------------------
// Linux: /proc/net/dev plus sysfs metadata
// ---------------------------------------------------------------------------

#if defined(SYSMON_LINUX)

std::vector<NetworkStats> NetworkMonitor::read_linux() {
    auto net_dev = utils::read_file("/proc/net/dev");
    if (!net_dev.has_value()) return {};

    std::vector<NetworkStats> result;
    std::istringstream iss(net_dev.value());
    std::string line;

    std::getline(iss, line);   // "Inter-|   Receive …"
    std::getline(iss, line);   // " face |bytes packets …"

    while (std::getline(iss, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        const std::string iface = utils::trim(line.substr(0, colon));
        const auto parts = utils::split_whitespace(line.substr(colon + 1));
        if (parts.size() < 16) continue;

        auto field = [&parts](size_t i) -> uint64_t {
            const auto v = utils::to_int(parts[i]);
            return (v.has_value() && *v >= 0) ? static_cast<uint64_t>(*v) : 0ULL;
        };

        NetworkStats ns;
        ns.name             = iface;
        ns.rx_bytes_total   = field(0);
        ns.rx_packets_total = field(1);
        ns.rx_errors        = field(2);
        ns.rx_dropped       = field(3);
        ns.tx_bytes_total   = field(8);
        ns.tx_packets_total = field(9);
        ns.tx_errors        = field(10);
        ns.tx_dropped       = field(11);

        const std::string sys = "/sys/class/net/" + iface;
        ns.mac_address = utils::read_first_line(sys + "/address").value_or("");

        if (auto mtu = utils::read_first_line(sys + "/mtu")) {
            if (auto v = utils::to_int(*mtu)) ns.mtu = static_cast<uint32_t>(*v);
        }
        if (auto speed = utils::read_first_line(sys + "/speed")) {
            // Virtual and down interfaces report -1 here; that is "unknown",
            // not a speed.
            if (auto v = utils::to_int(*speed)) {
                if (*v > 0) ns.speed_mbps = static_cast<uint64_t>(*v);
            }
        }
        if (auto duplex = utils::read_first_line(sys + "/duplex")) {
            if (*duplex != "unknown") ns.duplex = *duplex;
        }
        if (auto state = utils::read_first_line(sys + "/operstate")) {
            ns.is_up = (*state == "up");
        }

        std::error_code ec;
        ns.is_wireless  = std::filesystem::exists(sys + "/wireless", ec);
        ns.is_loopback  = (iface == "lo");

        result.push_back(std::move(ns));
    }

    fill_addresses_posix(result);
    return result;
}

#else
std::vector<NetworkStats> NetworkMonitor::read_linux() { return {}; }
#endif

// ---------------------------------------------------------------------------
// macOS: getifaddrs() AF_LINK counters
// ---------------------------------------------------------------------------

#if defined(SYSMON_MACOS)

std::vector<NetworkStats> NetworkMonitor::read_macos() {
    struct ifaddrs* addrs = nullptr;
    if (getifaddrs(&addrs) != 0) return {};

    std::vector<NetworkStats> result;

    for (struct ifaddrs* ifa = addrs; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_name == nullptr || ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_addr->sa_family != AF_LINK) continue;

        const auto* data = reinterpret_cast<const struct if_data*>(ifa->ifa_data);
        if (data == nullptr) continue;

        NetworkStats ns;
        ns.name             = ifa->ifa_name;
        ns.rx_bytes_total   = data->ifi_ibytes;
        ns.tx_bytes_total   = data->ifi_obytes;
        ns.rx_packets_total = data->ifi_ipackets;
        ns.tx_packets_total = data->ifi_opackets;
        ns.rx_errors        = data->ifi_ierrors;
        ns.tx_errors        = data->ifi_oerrors;
        ns.rx_dropped       = data->ifi_iqdrops;
        ns.tx_dropped       = 0;
        ns.mtu              = static_cast<uint32_t>(data->ifi_mtu);
        ns.is_up            = (ifa->ifa_flags & IFF_UP) != 0 &&
                              (ifa->ifa_flags & IFF_RUNNING) != 0;
        ns.is_loopback      = (ifa->ifa_flags & IFF_LOOPBACK) != 0;

        // ifi_baudrate is the link's negotiated bit rate.  This is the value
        // that used to be reported as "N/A" on every macOS interface.
        if (data->ifi_baudrate > 0) {
            ns.speed_mbps = static_cast<uint64_t>(data->ifi_baudrate / 1'000'000ULL);
            if (*ns.speed_mbps == 0) ns.speed_mbps.reset();   // sub-Mbps link
        }

        const auto* sdl = reinterpret_cast<const struct sockaddr_dl*>(ifa->ifa_addr);
        if (sdl != nullptr && sdl->sdl_alen > 0) {
            ns.mac_address = format_mac(
                reinterpret_cast<const unsigned char*>(LLADDR(sdl)), sdl->sdl_alen);
        }

        // Media type tells Wi-Fi from Ethernet and carries the duplex mode.
        if (auto media = query_media_macos(ns.name)) {
            ns.is_wireless = (IFM_TYPE(media->ifm_active) == IFM_IEEE80211);
            if (IFM_OPTIONS(media->ifm_active) & IFM_FDX)      ns.duplex = "full";
            else if (IFM_OPTIONS(media->ifm_active) & IFM_HDX) ns.duplex = "half";
        }

        result.push_back(std::move(ns));
    }

    freeifaddrs(addrs);

    fill_addresses_posix(result);
    return result;
}

#else
std::vector<NetworkStats> NetworkMonitor::read_macos() { return {}; }
#endif

// ---------------------------------------------------------------------------
// Windows: GetIfTable2 + GetAdaptersAddresses
// ---------------------------------------------------------------------------

#if defined(SYSMON_WINDOWS)

std::vector<NetworkStats> NetworkMonitor::read_windows() {
    std::vector<NetworkStats> result;

    // -- Counters, MTU, speed and media type from the interface table -------
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) == NO_ERROR && table != nullptr) {
        for (ULONG i = 0; i < table->NumEntries; ++i) {
            const MIB_IF_ROW2& row = table->Table[i];

            std::string name = wide_to_utf8(row.Alias);
            if (name.empty()) name = wide_to_utf8(row.Description);
            if (name.empty()) continue;

            NetworkStats ns;
            ns.name             = name;
            ns.rx_bytes_total   = row.InOctets;
            ns.tx_bytes_total   = row.OutOctets;
            ns.rx_packets_total = row.InUcastPkts + row.InNUcastPkts;
            ns.tx_packets_total = row.OutUcastPkts + row.OutNUcastPkts;
            ns.rx_errors        = row.InErrors;
            ns.tx_errors        = row.OutErrors;
            ns.rx_dropped       = row.InDiscards;
            ns.tx_dropped       = row.OutDiscards;
            ns.mtu              = row.Mtu;
            ns.is_up            = (row.OperStatus == IfOperStatusUp);
            ns.is_loopback      = (row.Type == IF_TYPE_SOFTWARE_LOOPBACK);
            ns.is_wireless      = (row.Type == IF_TYPE_IEEE80211);

            if (row.TransmitLinkSpeed > 0 && row.TransmitLinkSpeed != UINT64_MAX) {
                const uint64_t mbps = row.TransmitLinkSpeed / 1'000'000ULL;
                if (mbps > 0) ns.speed_mbps = mbps;
            }
            if (row.PhysicalAddressLength > 0) {
                ns.mac_address = format_mac(row.PhysicalAddress, row.PhysicalAddressLength);
            }

            result.push_back(std::move(ns));
        }
        FreeMibTable(table);
    }

    // -- IP addresses from the adapter list --------------------------------
    const auto buffer = get_adapters_addresses();
    if (!buffer.empty()) {
        for (auto* adapter = reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buffer.data());
             adapter != nullptr; adapter = adapter->Next) {

            std::string name = wide_to_utf8(adapter->FriendlyName);
            if (name.empty()) name = wide_to_utf8(adapter->Description);
            if (name.empty()) continue;

            auto existing = std::find_if(result.begin(), result.end(),
                                         [&name](const NetworkStats& n) { return n.name == name; });
            if (existing == result.end()) continue;

            for (auto* unicast = adapter->FirstUnicastAddress;
                 unicast != nullptr; unicast = unicast->Next) {
                const std::string address = sockaddr_to_string(unicast->Address);
                if (address.empty()) continue;

                if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
                    if (existing->ip_address.empty()) existing->ip_address = address;
                } else if (existing->ip6_address.empty() ||
                           utils::starts_with(existing->ip6_address, "fe80")) {
                    existing->ip6_address = address;
                }
            }
        }
    }

    return result;
}

#else
std::vector<NetworkStats> NetworkMonitor::read_windows() { return {}; }
#endif

// ---------------------------------------------------------------------------
// Host-wide facts: default gateway and DNS servers
// ---------------------------------------------------------------------------

std::string NetworkMonitor::parse_proc_net_route(const std::string& content) {
    std::istringstream iss(content);
    std::string line;
    std::getline(iss, line);   // header

    while (std::getline(iss, line)) {
        const auto parts = utils::split_whitespace(line);
        // Iface Destination Gateway Flags RefCnt Use Metric Mask …
        if (parts.size() < 3) continue;
        if (parts[1] != "00000000") continue;   // not the default route

        // The gateway is a little-endian hex word.
        unsigned long value = 0;
        try {
            value = std::stoul(parts[2], nullptr, 16);
        } catch (...) {
            continue;
        }
        if (value == 0) continue;

        return std::to_string(value & 0xFF) + "." +
               std::to_string((value >> 8) & 0xFF) + "." +
               std::to_string((value >> 16) & 0xFF) + "." +
               std::to_string((value >> 24) & 0xFF);
    }
    return "";
}

std::vector<std::string> NetworkMonitor::parse_resolv_conf(const std::string& content) {
    std::vector<std::string> servers;
    std::istringstream iss(content);
    std::string line;

    while (std::getline(iss, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        const auto parts = utils::split_whitespace(line);
        if (parts.size() >= 2 && parts[0] == "nameserver") {
            if (std::find(servers.begin(), servers.end(), parts[1]) == servers.end()) {
                servers.push_back(parts[1]);
            }
        }
    }
    return servers;
}

NetGlobalStats NetworkMonitor::read_global() {
    NetGlobalStats global;

#if defined(SYSMON_LINUX)
    if (auto route = utils::read_file("/proc/net/route")) {
        global.default_gateway_v4 = parse_proc_net_route(route.value());
    }
    if (auto resolv = utils::read_file("/etc/resolv.conf")) {
        global.dns_servers = parse_resolv_conf(resolv.value());
    }

#elif defined(SYSMON_MACOS)
    // macOS has no /proc/net/route; the routing table is only reachable through
    // sysctl or the route command, and the latter is far simpler to parse.
    if (auto route = utils::run_command("route -n get default")) {
        std::istringstream iss(route.value());
        std::string line;
        while (std::getline(iss, line)) {
            const auto parts = utils::split_whitespace(line);
            if (parts.size() >= 2 && parts[0] == "gateway:") {
                global.default_gateway_v4 = parts[1];
            }
        }
    }
    if (auto route6 = utils::run_command("route -n get -inet6 default")) {
        std::istringstream iss(route6.value());
        std::string line;
        while (std::getline(iss, line)) {
            const auto parts = utils::split_whitespace(line);
            if (parts.size() >= 2 && parts[0] == "gateway:") {
                global.default_gateway_v6 = parts[1];
            }
        }
    }
    if (auto resolv = utils::read_file("/etc/resolv.conf")) {
        global.dns_servers = parse_resolv_conf(resolv.value());
    }

#elif defined(SYSMON_WINDOWS)
    const auto buffer = get_adapters_addresses();
    if (!buffer.empty()) {
        for (auto* adapter = reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buffer.data());
             adapter != nullptr; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp) continue;

            for (auto* gateway = adapter->FirstGatewayAddress;
                 gateway != nullptr; gateway = gateway->Next) {
                const std::string address = sockaddr_to_string(gateway->Address);
                if (address.empty()) continue;
                if (gateway->Address.lpSockaddr->sa_family == AF_INET) {
                    if (global.default_gateway_v4.empty()) global.default_gateway_v4 = address;
                } else if (global.default_gateway_v6.empty()) {
                    global.default_gateway_v6 = address;
                }
            }

            for (auto* dns = adapter->FirstDnsServerAddress; dns != nullptr; dns = dns->Next) {
                const std::string address = sockaddr_to_string(dns->Address);
                if (address.empty()) continue;
                if (std::find(global.dns_servers.begin(), global.dns_servers.end(), address) ==
                    global.dns_servers.end()) {
                    global.dns_servers.push_back(address);
                }
            }

            if (global.domain.empty()) {
                global.domain = wide_to_utf8(adapter->DnsSuffix);
            }
        }
    }
#endif

    return global;
}
