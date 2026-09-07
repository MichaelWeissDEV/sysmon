# Introduction

**sysmon** is a modern, deep system monitor and live terminal dashboard written
in modern C++20. It runs on **Linux**, **macOS** (Intel & Apple Silicon) and
**Windows**, offering granular visibility into your machine from a single
dependency-free binary.

## Design rule: no invented numbers

Every metric sysmon prints is either read from a stable, unprivileged operating
system interface or reported as `N/A`. sysmon never substitutes a
plausible-looking estimate for a value it could not measure. In `--json` output
the same rule appears as `null` rather than `0`, so a consumer can tell "this
platform does not expose it" apart from "it was measured and it is zero".

---

## Key Capabilities

- **Per-Core CPU Monitoring:** Individual core loads, the full user/sys/idle/iowait/nice/irq/steal split, cache topology, context-switch and interrupt rates, and live sparkline graphs. On hybrid CPUs each core is labelled as a performance or efficiency core.
- **GPU & Unified Memory Monitoring:** Reports the GPU vendor/model and, on Apple Silicon, the unified memory capacity shared with the CPU. GPU core count is reported only when the kernel exposes it (`hw.gpu.count`); GPU usage and clock frequencies are not published by Apple and are reported as N/A.
- **Network Traffic & Active Connections:** Live per-interface throughput rates, IPv4/IPv6 addresses, plus socket mapping (TCP/UDP endpoints connected to local PIDs and process names on macOS).
- **Storage & Disk I/O:** Monitored filesystem capacities, mount points, filesystem types, and real-time disk read/write throughput (B/s, KB/s, MB/s).
- **Process Hierarchy:** Top consumers with user attribution, PPID, thread counts, RSS/VMS memory, accumulated CPU time, nice level, open file count, per-process disk I/O and process states — sortable by CPU, memory, PID, name or CPU time.
- **Battery & Power:** Charge level, charging state, time remaining, health against design capacity, cycle count, voltage, instantaneous draw and battery temperature.
- **JSON Output:** `--json` emits every collected field as a machine-readable document for scripting, alerting and time-series capture.
- **Hardware Temperatures:** Full sensor reporting on Linux via `hwmon`/thermal zones with warning/critical threshold indicators. On macOS, CPU package temperature is not publicly available, so it is reported as N/A; battery temperature is read via IOKit when the sensor is present.
- **Dynamic Customization & Config:** Switch between detailed and compact summary dashboards, toggle sections with hotkeys or configure via the config file (`~/.config/sysmon/sysmon.conf`, or `$XDG_CONFIG_HOME`).
- **Zero External Runtime Dependencies:** Pure C++20 using kernel pseudo-filesystems and native POSIX/Mach/Win32 APIs with zero third-party library overhead.

---

## Platform Support

| Platform | Interfaces used |
|----------|-----------------|
| Linux | `/proc`, `/sys`, hwmon, `getifaddrs`, `statvfs` |
| macOS | sysctl, Mach host/task info, IOKit, `getifaddrs`, `getmntinfo` |
| Windows | NT query APIs, IP Helper, Toolhelp32, PSAPI, PowrProf, DXGI |

Metrics without a stable unprivileged API on a given platform are reported as
`N/A`; see the availability table in the project README.

---

## Live Dashboard Preview

```
 ⬡ sysmon v0.2.0  macOS 26.4 (arm64)      MacBook-Pro-von-Michael.local  up: 1h 14m 02s  17:48:15 
── CPU ──────────────────────────────────────────────────────────────────────────────────────────
 Model    Apple M4 Max
 Cores    16 logical / 16 physical
 Usage    14.2%  [█████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]   ▂▃▅▄▃▂▁▃▄▅
 Breakdown usr 11.0% | sys 3.2% | iowait 0.0% | idle 85.8%

 Individual Cores:
 C 0 [██░░░░░░]  24%  C 1 [█░░░░░░░]  12%  C 2 [███░░░░░]  38%  C 3 [█░░░░░░░]  10% 
 C 4 [██░░░░░░]  20%  C 5 [█░░░░░░░]  15%  C 6 [██░░░░░░]  22%  C 7 [█░░░░░░░]  14% 

── GPU / Graphics ──────────────────────────────────────────────────────────────────────────────
 Apple M4 Max GPU [Apple]
   System unified-memory capacity  128.0 GB

── Memory (RAM & Swap) ─────────────────────────────────────────────────────────────────────────
 RAM     24.1 GB / 128.0 GB  [███████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]   18.8%  cache: 13.7 GB

── Load Average ────────────────────────────────────────────────────────────────────────────────
 1 min  2.45    5 min  2.60    15 min  2.35    Procs: 3 running / 705 total

── Network Interfaces ──────────────────────────────────────────────────────────────────────────
 ▲ en0        192.168.0.4       ↓   1.4 MB/s  ▃▅▆▇█  ↑ 120.5 KB/s  ▂▃▄▅
 ▲ en7        192.168.0.5       ↓      0.0 B/s        ↑      0.0 B/s

── Active Network Connections ──────────────────────────────────────────────────────────────────
  PROTO LOCAL ADDRESS          REMOTE ADDRESS         STATE         PID     PROCESS
  tcp4  192.168.0.4:49584      34.54.84.110:443       ESTABLISHED   8686    antigravity
  tcp4  192.168.0.4:49696      140.82.114.25:443      ESTABLISHED   5378    git

── Storage & Disk I/O ──────────────────────────────────────────────────────────────────────────
 /                  apfs    765.2 GB / 926.4 GB [█████████████████████████████████░░░░░░░]  82.6%
 /Volumes/Backup    apfs    1.2 TB / 2.0 TB     [████████████████████████░░░░░░░░░░░░░░░░]  60.1%

── Top Processes ───────────────────────────────────────────────────────────────────────────────
     PID  COMMAND             USER            CPU%     MEM%       RSS   THR  S
    8686  antigravity         michael         14.2      3.8    4.8 GB    42  R
    5378  firefox             michael          4.1      1.2    1.5 GB    28  S
────────────────────────────────────────── [c]cores [g]gpu [n]net [v]conn [p]proc [m]compact [q]quit
```