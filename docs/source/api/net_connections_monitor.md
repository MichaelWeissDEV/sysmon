# NetConnectionsMonitor

## Overview

`NetConnectionsMonitor` tracks active TCP and UDP sockets, network endpoints, connection states, and maps them to process IDs and names.

**Header:** `include/sysmon/net_connections_monitor.hpp`  
**Source:** `src/net_connections_monitor.cpp`

## Class Definition

```cpp
class NetConnectionsMonitor {
public:
    NetConnectionsMonitor();
    std::vector<NetConnectionStats> read(bool include_listen = false, unsigned int limit = 100);
};
```

## `NetConnectionStats`

| Field | Type | Description |
|---|---|---|
| `protocol` | `std::string` | "TCP", "TCP6", "UDP", "UDP6" |
| `local_addr` | `std::string` | Local IP address (IPv4 or IPv6) |
| `local_port` | `uint16_t` | Local port number |
| `remote_addr` | `std::string` | Remote peer IP address |
| `remote_port` | `uint16_t` | Remote port number (0 if listening / unconn) |
| `state` | `std::string` | "ESTABLISHED", "LISTEN", "TIME_WAIT", "CLOSE_WAIT", "SYN_SENT", etc. |
| `pid` | `int` | Associated Process ID (-1 if unmapped) |
| `process_name` | `std::string` | Process command name owning the socket |

## Platform Implementation Details

- **Linux:** Parses `/proc/net/tcp`, `/proc/net/tcp6`, `/proc/net/udp`, and `/proc/net/udp6`. Maps socket inodes to running processes by inspecting `/proc/<pid>/fd/` symlinks and resolves names via `/proc/<pid>/comm`.
- **macOS:** Queries active networking subsystem sockets and resolves process associations.

## Example

```cpp
#include "sysmon/net_connections_monitor.hpp"
#include <iostream>

int main() {
    NetConnectionsMonitor mon;
    auto conns = mon.read(false, 10); // exclude LISTEN sockets, top 10

    for (const auto& c : conns) {
        std::cout << c.protocol << " "
                  << c.local_addr << ":" << c.local_port << " -> "
                  << c.remote_addr << ":" << c.remote_port
                  << " [" << c.state << "] "
                  << "PID " << c.pid << " (" << c.process_name << ")\n";
    }
    return 0;
}
```
