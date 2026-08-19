# Introduction

**sysmon** is a modern, deep system monitor and live terminal dashboard written in modern C++20. It runs seamlessly across **Linux** and **macOS** (Intel & Apple Silicon), offering granular visibility into every component of your machine.

---

## Key Capabilities

- **Per-Core CPU Monitoring:** View individual core loads, aggregate user/sys/iowait time, and live sparkline graphs.
- **GPU & Unified Memory Monitoring:** Real-time visibility into GPU compute cores, Unified Memory (Apple Silicon M1/M2/M3/M4 series), Dedicated VRAM (AMD / NVIDIA), and clock frequencies.
- **Network Traffic & Active Connections:** Live per-interface throughput rates, IPv4/IPv6 addresses, link speeds, plus socket mapping (TCP/UDP endpoints connected to local PIDs and process names).
- **Storage & Disk I/O:** Monitored filesystem capacities, mount points, filesystem types, and real-time disk read/write throughput (B/s, KB/s, MB/s).
- **Process Hierarchy:** Top CPU/Memory consumers with user attribution, thread counts, RSS/VMS memory, and process states.
- **Hardware Temperatures:** Hardware monitoring across all `hwmon` and thermal zones with warning/critical threshold indicators.
- **Dynamic Customization & Config:** Switch between deep multi-panel views and compact summary dashboards, toggle sections with hotkeys or configure via `~/.config/sysmon/sysmon.conf`.
- **Zero External Runtime Dependencies:** Pure C++20 using kernel pseudo-filesystems and native POSIX/Mach APIs with zero third-party library overhead.

---

## Live Dashboard Preview

```
 ⬡ sysmon v0.2.0  macOS 26.4 (arm64)      MacBook-Pro-von-Michael.local  up: 1h 14m 02s  17:48:15 
── CPU ──────────────────────────────────────────────────────────────────────────────────────────
 Model    Apple M4 Max
 Cores    16 logical / 16 physical  @0 MHz
 Usage    14.2%  [█████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]   ▂▃▅▄▃▂▁▃▄▅
 Breakdown usr 11.0% | sys 3.2% | iowait 0.0% | idle 85.8%

 Individual Cores:
 C 0 [██░░░░░░]  24%  C 1 [█░░░░░░░]  12%  C 2 [███░░░░░]  38%  C 3 [█░░░░░░░]  10% 
 C 4 [██░░░░░░]  20%  C 5 [█░░░░░░░]  15%  C 6 [██░░░░░░]  22%  C 7 [█░░░░░░░]  14% 

── GPU / Graphics ──────────────────────────────────────────────────────────────────────────────
 Apple M4 Max GPU [Apple] (40 GPU Cores)
   Unified Memory   4.2 GB / 128.0 GB  [█░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]   3.3%

── Memory (RAM & Swap) ─────────────────────────────────────────────────────────────────────────
 RAM     24.1 GB / 128.0 GB  [███████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]   18.8%  cache: 13.7 GB

── Load Average ────────────────────────────────────────────────────────────────────────────────
 1 min  2.45    5 min  2.60    15 min  2.35    Procs: 0 running / 705 total

── Network Interfaces ──────────────────────────────────────────────────────────────────────────
 ▲ en0        192.168.0.4       ↓   1.4 MB/s  ▃▅▆▇█  ↑ 120.5 KB/s  ▂▃▄▅
 ▲ en7        192.168.0.5       ↓      0.0 B/s        ↑      0.0 B/s

── Active Network Connections ──────────────────────────────────────────────────────────────────
  PROTO LOCAL ADDRESS          REMOTE ADDRESS         STATE         PID     PROCESS
  tcp4  192.168.0.5:49696      34.54.84.110:443       ESTABLISHED   131072  antigravity
  tcp4  192.168.0.5:49834      140.82.114.25:443      ESTABLISHED   131072  git

── Storage & Disk I/O ──────────────────────────────────────────────────────────────────────────
 /                  apfs    765.2 GB / 926.4 GB [█████████████████████████████████░░░░░░░]  82.6%
 /System/Volumes/Data apfs  765.2 GB / 926.4 GB [█████████████████████████████████░░░░░░░]  82.6%

── Top Processes ───────────────────────────────────────────────────────────────────────────────
     PID  COMMAND             USER            CPU%     MEM%       RSS   THR  S
  131072  antigravity         michael         14.2      3.8    4.8 GB    42  R
    1234  Code Helper         michael          4.1      1.2    1.5 GB    28  S
────────────────────────────────────────── [c]cores [g]gpu [n]net [v]conn [p]proc [m]compact [q]quit
```
