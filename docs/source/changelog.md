# Changelog

## Unreleased

### Windows support

- **Windows is now a fully implemented platform**, not a planned one. Every
  monitor has a Win32 implementation: per-core CPU times via
  `NtQuerySystemInformation`, memory via `GlobalMemoryStatusEx` and
  `GetPerformanceInfo`, disks via `GetDiskFreeSpaceEx` and
  `IOCTL_DISK_PERFORMANCE`, network via `GetIfTable2` and `GetAdaptersAddresses`,
  connections via `GetExtendedTcpTable`, processes via Toolhelp32 and PSAPI,
  GPU via DXGI, and battery via `GetSystemPowerStatus`.
- **New `terminal` abstraction** (`include/sysmon/terminal.hpp`) replaces the
  unconditional `termios.h` use in `main.cpp`. On Windows it enables
  virtual-terminal processing and UTF-8 console output, without which every ANSI
  escape and box-drawing glyph in the dashboard would print as literal garbage.
- **CI** gained a Windows/MSVC job that builds, tests and runs the binary, plus a
  MinGW cross-compile job.

### New metrics

- **Battery and power**: charge, state, time remaining, health against design
  capacity, cycle count, voltage, power draw and temperature, on all three
  platforms.
- **CPU**: vendor, socket count, threads per core, hybrid P/E core split and a
  per-core cluster label, cache sizes (L1d/L1i/L2/L3), context-switch, interrupt
  and fork rates, instruction-set feature flags, and the macOS thermal pressure
  level (reported as a constraint level, never as a temperature).
- **Memory**: active/inactive/wired/compressed breakdown, shared, slab, dirty,
  commit charge, page-fault and paging rates, and memory pressure.
- **Disks**: inode usage, mount options, read-only and removable flags,
  per-device IOPS, utilisation and average request latency.
- **Network**: MAC, MTU, duplex, packet rates, error and drop counters, the
  default gateway, DNS servers, and a socket-state census.
- **Processes**: PPID, nice level, accumulated CPU time, open file count,
  per-process disk I/O, command line, and sorting by cpu/mem/pid/name/time.
- **System**: OS build, machine model, boot time, timezone, logged-in user
  count, and hypervisor/container detection.

### New output modes

- **`--json` / `--json-compact`** emit every collected field as a machine-readable
  document. Metrics a platform cannot measure are `null`, never `0`, so a
  consumer can distinguish "unsupported" from "measured zero".
- **`--sort`**, **`--net-details`**, **`--all-interfaces`**, **`--listen`** and
  **`--battery`/`--no-battery`** flags.

### Bug fixes

- **macOS process CPU times were 41x too low.** `proc_pidinfo()` reports task
  times in mach absolute-time ticks, which equal nanoseconds on Intel but not on
  Apple Silicon (the timebase there is 125/3). The values are now converted
  through `mach_timebase_info()`, and match `ps -o time` exactly.
- **macOS reported every process as "running".** `kinfo_proc::p_stat` is `SRUN`
  for essentially every process because run state lives on the thread, so the
  load section showed "0 running" and the process table showed a column of `R`.
  Run state now comes from `pti_numrunning`.
- **Table columns could not overflow into one another.** `std::setw` pads but
  never truncates, so a long mountpoint produced
  `/System/Volumes/Data588.5 GB` and a 22-character process name ran straight
  into the user column. All text columns now go through a UTF-8-aware
  `utils::column()` that truncates on codepoint boundaries and always leaves a
  separating space.
- **macOS listed the same storage several times.** `/`, `/System/Volumes/Data`
  and the update snapshots share one APFS container and each reported the full
  capacity. Volumes are now deduplicated by backing device.
- **macOS network link speed was always `N/A`.** It is available as
  `if_data::ifi_baudrate` from `getifaddrs()`.
- **`--config` discarded earlier flags.** It replaced the whole configuration
  mid-parse, so `sysmon --no-gpu --config f` silently dropped `--no-gpu`.
  Configuration is now resolved in a first pass before any flag is applied.
- **Unknown options were silently ignored**, so `--limt 5` looked as though it
  had worked. They are now an error with exit status 2, as are invalid values.
- **`/proc/meminfo` keys were matched by prefix**, so `Active` could match
  `Active(anon)`. Field names are now compared in full.
- **`/proc/meminfo` was re-read and re-parsed once per process** while building
  the Linux process list.
- **The process snapshot map was cleared wholesale** when it grew past a
  threshold, discarding the deltas of live processes; dead entries are now pruned
  individually.
- **The JSON export was shaped by the dashboard's configuration.** A display
  toggle emptied its section (`--json --no-proc` produced `"processes": []`,
  indistinguishable from "no processes exist"), and the `proc_limit` /
  `connections_limit` settings truncated the arrays — so a stale value in
  `~/.config/sysmon/sysmon.conf` silently shortened every snapshot a monitoring
  pipeline collected. `--json` now always collects every section in full; an
  explicit `--limit N` on the command line is still honoured.
- **`/proc/diskstats` rows with fewer than 14 fields were skipped entirely.**
  Only the first 10 fields are guaranteed; the timing and busy fields that
  follow are not published by every device or kernel version, and requiring them
  meant such devices reported no disk I/O at all. The base fields are now
  sufficient and the rest are treated as optional.
- **Interface up/down state was decided twice on Linux**, with the weaker
  `IFF_UP|IFF_RUNNING` flags overwriting the sysfs `operstate` that had already
  been read.
- **Test discovery could fail the build at random.** `gtest_discover_tests`
  defaults to a 5-second timeout whose expiry is a build error that deletes the
  test binary; a loaded parallel build on a shared CI runner beat it often
  enough to red-light the matrix. Raised to 60 seconds.
- Disk usage is clamped and underflow-guarded, so a filesystem reporting more
  free blocks than total blocks can no longer produce a nonsensical figure.
- IPv6 addresses now prefer a routable address over the link-local one.

### Improvements

- **Build system hardening** — Target-based `CMakeLists.txt` with `cxx_std_20`, target-private warning flags, `SYSMON_WARNINGS_AS_ERRORS` option, and a generated `version.hpp` single-sourcing the version.
- **Portable parallel builds** — `Makefile` no longer relies on `nproc`/`sysctl`, using `cmake --build --parallel` instead.
- **Configuration path** — Config now honors `$XDG_CONFIG_HOME` with fallback to `~/.config/sysmon/sysmon.conf`.
- **GitHub Actions CI** — Linux, macOS (Apple Silicon), macOS (Intel), ASan+UBSan, and Sphinx documentation jobs.

### macOS fixes

- **No fabricated temperatures** — The thermal-pressure-to-Celsius conversion (`45.0 + thermal_level * 15.0`) was removed. CPU package temperature is reported as N/A on macOS; battery temperature is read via IOKit (`AppleSmartBattery`) when present.
- **Honest GPU reporting** — Removed the hardcoded Apple M-chip GPU-core lookup table and the fake "wired + compressed RAM = GPU memory used" formula. Only `hw.gpu.count` (cores) and `hw.memsize` (unified capacity) are reported; usage, frequency, and memory usage are N/A.
- **CPU frequency** — Reported only from valid `hw.cpufrequency*` sysctls; no timebase derivation, so Apple Silicon reports N/A.
- **CPU usage semantics** — Normalized so `100 % = one fully utilized logical core` on both platforms; hardened against counter resets.
- **Network connections** — Parsers rewritten against real `netstat -anv` output (TCP state column, token-spanning `process:pid`); no fabricated PIDs.
- **Rate underflow guards** — Network and disk I/O deltas no longer underflow when counters reset.
- **Robust process enumeration** — macOS sysctl retry loop and per-process fault tolerance.

### Bug Fixes

- Fixed duplicate `enable_testing()` in CMakeLists.txt.
- Invalid config values fall back to defaults instead of crashing.
- Corrected test expectations (bytes formatting, CPU delta math, netstat wildcard addresses).

---

## v0.1.0 (initial)

- CPU, memory, load, disk, temperature (Linux only)
- Plain text output
- CMake build