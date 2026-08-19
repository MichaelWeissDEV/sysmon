# NetworkMonitor

## Overview

`NetworkMonitor` collects per-interface network statistics including byte throughput, packet counts, errors, IP addresses, and link speed.

**Header:** `include/sysmon/network_monitor.hpp`  
**Source:** `src/network_monitor.cpp`

## Class

```cpp
class NetworkMonitor {
public:
    NetworkMonitor();
    std::vector<NetworkStats> read();
};
```

## `NetworkStats`

| Field | Type | Description |
|-------|------|-------------|
| `interface` | `std::string` | Interface name (e.g. "eth0", "en0") |
| `rx_bytes_total` | `uint64_t` | Cumulative bytes received |
| `tx_bytes_total` | `uint64_t` | Cumulative bytes transmitted |
| `rx_packets_total` | `uint64_t` | Cumulative packets received |
| `tx_packets_total` | `uint64_t` | Cumulative packets transmitted |
| `rx_errors` | `uint64_t` | Receive errors |
| `tx_errors` | `uint64_t` | Transmit errors |
| `rx_bytes_per_sec` | `double` | Current receive throughput (B/s) |
| `tx_bytes_per_sec` | `double` | Current transmit throughput (B/s) |
| `ip_address` | `std::string` | IPv4 address |
| `ip6_address` | `std::string` | IPv6 address |
| `is_up` | `bool` | Interface up/down state |
| `speed_mbps` | `uint64_t` | Link speed in Mbps (0 = unknown) |

## Platform Notes

| Platform | Data Source |
|----------|-------------|
| Linux | `/proc/net/dev`, `/sys/class/net/<iface>/speed`, `getifaddrs()` |
| macOS | `getifaddrs()`, `AF_LINK` socket data |

> **Rate calculation:** The monitor stores the previous sample internally.  
> The first call returns zero rates. Subsequent calls compute delta / elapsed time.

## Example

```cpp
NetworkMonitor mon;
mon.read();  // initialise internal state

std::this_thread::sleep_for(std::chrono::seconds(1));

auto ifaces = mon.read();
for (const auto& n : ifaces) {
    std::cout << n.interface
              << "  ↓ " << n.rx_bytes_per_sec / 1024.0 << " KB/s"
              << "  ↑ " << n.tx_bytes_per_sec / 1024.0 << " KB/s\n";
}
```
