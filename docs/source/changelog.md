# Changelog

## Unreleased

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