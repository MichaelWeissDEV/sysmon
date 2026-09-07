#include "sysmon/text_renderer.hpp"
#include "sysmon/utils.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

/// Label column width for the "  Label   value" lines.
constexpr std::size_t kLabel = 16;

std::string label(const std::string& text) {
    return "  " + utils::fit(text, kLabel);
}

/// Render an optional number, or "N/A" when the platform could not measure it.
template <typename T>
std::string opt(const std::optional<T>& value, const std::string& suffix = "",
                int precision = 1) {
    if (!value.has_value()) return "N/A";
    std::ostringstream oss;
    if constexpr (std::is_floating_point_v<T>) {
        oss << std::fixed << std::setprecision(precision) << value.value();
    } else {
        oss << value.value();
    }
    if (!suffix.empty()) oss << " " << suffix;
    return oss.str();
}

std::string percent(double value, int precision = 1) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value << " %";
    return oss.str();
}

std::string number(double value, int precision = 1) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

} // namespace

void TextRenderer::render(const Snapshot& snap, const Config& cfg) {
    render_to(std::cout, snap, cfg);
}

void TextRenderer::render_to(std::ostream& out, const Snapshot& snap, const Config& cfg) {
    render_system(out, snap.system);
    out << "\n";

    if (cfg.show_cpu) {
        render_cpu(out, snap.cpu, cfg);
        out << "\n";
    }
    if (cfg.show_gpu && !snap.gpus.empty()) {
        render_gpu(out, snap.gpus, cfg);
        out << "\n";
    }
    if (cfg.show_memory) {
        render_memory(out, snap.memory, cfg);
        out << "\n";
    }

    render_load(out, snap.load);
    out << "\n";

    if (cfg.show_battery && snap.battery.present) {
        render_battery(out, snap.battery);
        out << "\n";
    }
    if (cfg.show_temperature) {
        render_temperatures(out, snap.temperatures, cfg);
    }
    if (cfg.show_disk) {
        render_disks(out, snap.disks, snap.disk_io, cfg);
        out << "\n";
    }
    if (cfg.show_network) {
        render_network(out, snap.network, snap.net_global, cfg);
        out << "\n";
    }
    if (cfg.show_connections && !snap.connections.empty()) {
        render_connections(out, snap.connections, cfg);
        out << "\n";
    }
    if (cfg.show_processes && !snap.processes.empty()) {
        render_processes(out, snap.processes, cfg);
    }
}

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------

void TextRenderer::render_system(std::ostream& out, const SystemStats& s) {
    out << "System\n";
    out << label("Hostname")     << s.hostname << "\n";
    out << label("OS")           << s.os;
    if (!s.os_build.empty()) out << " (build " << s.os_build << ")";
    out << "\n";
    out << label("Kernel")       << s.kernel << "\n";
    out << label("Architecture") << s.architecture << "\n";
    if (!s.machine_model.empty()) out << label("Model") << s.machine_model << "\n";
    out << label("Uptime")       << s.uptime << "\n";
    if (!s.boot_time.empty())    out << label("Booted")  << s.boot_time << "\n";
    if (!s.current_time.empty()) {
        out << label("Local time") << s.current_time;
        if (!s.timezone.empty()) out << " " << s.timezone;
        out << "\n";
    }
    if (!s.virtualization.empty() && s.virtualization != "none") {
        out << label("Virtualized") << s.virtualization << "\n";
    }
    if (s.logged_in_users.has_value()) {
        out << label("Users") << s.logged_in_users.value() << " logged in\n";
    }
    if (s.process_count.has_value()) {
        out << label("Processes") << s.process_count.value();
        if (s.thread_count.has_value()) out << " (" << s.thread_count.value() << " threads)";
        out << "\n";
    }
}

// ---------------------------------------------------------------------------
// CPU
// ---------------------------------------------------------------------------

void TextRenderer::render_cpu(std::ostream& out, const CpuStats& s, const Config& cfg) {
    out << "CPU\n";
    out << label("Model") << s.model << "\n";
    if (!s.vendor.empty()) out << label("Vendor") << s.vendor << "\n";

    out << label("Cores") << s.logical_cores << " logical / " << s.physical_cores << " physical";
    if (s.performance_cores.has_value() && s.efficiency_cores.has_value()) {
        out << "  (" << s.performance_cores.value() << "P + "
            << s.efficiency_cores.value() << "E)";
    }
    if (s.sockets.has_value() && s.sockets.value() > 1) {
        out << "  across " << s.sockets.value() << " sockets";
    }
    out << "\n";
    if (s.threads_per_core.has_value()) {
        out << label("Threads/core") << s.threads_per_core.value() << "\n";
    }

    out << label("Usage") << percent(s.usage_percent) << "\n";
    if (cfg.show_cpu_cores_detail) {
        out << label("usr/sys") << percent(s.user_percent) << " / " << percent(s.system_percent) << "\n";
        out << label("idle/iowait") << percent(s.idle_percent) << " / " << percent(s.iowait_percent) << "\n";
        if (s.steal_percent > 0.0) out << label("steal") << percent(s.steal_percent) << "\n";
    }

    out << label("Frequency");
    if (s.frequency_mhz.has_value()) {
        out << number(s.frequency_mhz.value(), 0) << " MHz";
        if (s.max_frequency_mhz.has_value()) {
            out << " / " << number(s.max_frequency_mhz.value(), 0) << " MHz max";
        }
    } else if (s.base_frequency_mhz.has_value()) {
        out << number(s.base_frequency_mhz.value(), 0) << " MHz base";
    } else {
        out << "N/A";
    }
    out << "\n";

    out << label("Temperature") << opt(s.temperature_celsius, "°C") << "\n";
    if (!s.thermal_pressure.empty()) {
        // Not a temperature: this is the OS's own thermal-constraint level.
        out << label("Thermal state") << s.thermal_pressure << "\n";
    }

    if (s.cache_l1d_bytes || s.cache_l2_bytes || s.cache_l3_bytes) {
        out << label("Cache");
        bool first = true;
        auto emit = [&](const char* name, const std::optional<uint64_t>& value) {
            if (!value.has_value()) return;
            if (!first) out << "  ";
            out << name << " " << utils::format_bytes(value.value());
            first = false;
        };
        emit("L1d", s.cache_l1d_bytes);
        emit("L1i", s.cache_l1i_bytes);
        emit("L2",  s.cache_l2_bytes);
        emit("L3",  s.cache_l3_bytes);
        out << "\n";
    }

    if (s.context_switches_per_sec.has_value()) {
        out << label("Ctx switches") << utils::format_rate(s.context_switches_per_sec.value(), "/s") << "\n";
    }
    if (s.interrupts_per_sec.has_value()) {
        out << label("Interrupts") << utils::format_rate(s.interrupts_per_sec.value(), "/s") << "\n";
    }
    if (s.forks_per_sec.has_value()) {
        out << label("New processes") << utils::format_rate(s.forks_per_sec.value(), "/s") << "\n";
    }

    if (!s.flags.empty()) {
        std::ostringstream flags;
        for (size_t i = 0; i < s.flags.size(); ++i) {
            if (i > 0) flags << " ";
            flags << s.flags[i];
        }
        out << label("Features") << utils::truncate(flags.str(), 96) << "\n";
    }

    if (cfg.show_cpu_per_core && !s.per_core.empty()) {
        out << "  Per Core:\n";
        for (const auto& c : s.per_core) {
            out << "    " << utils::fit("Core " + std::to_string(c.id), 10)
                << utils::fit_right(percent(c.usage_percent), 8);
            if (!c.cluster.empty()) out << "  [" << c.cluster << "]";
            if (c.frequency_mhz.has_value()) {
                out << "  " << number(c.frequency_mhz.value(), 0) << " MHz";
            }
            if (c.temperature_celsius.has_value()) {
                out << "  " << number(c.temperature_celsius.value()) << " °C";
            }
            out << "\n";
        }
    }
}

// ---------------------------------------------------------------------------
// GPU
// ---------------------------------------------------------------------------

void TextRenderer::render_gpu(std::ostream& out, const std::vector<GpuStats>& gpus,
                              const Config& cfg) {
    out << "GPU / Graphics\n";
    for (const auto& g : gpus) {
        out << "  " << g.name << " [" << g.vendor << "]\n";
        if (g.gpu_cores.has_value()) {
            out << "    GPU Cores   " << g.gpu_cores.value() << "\n";
        }
        if (!g.driver_version.empty()) {
            out << "    Driver      " << g.driver_version << "\n";
        }
        out << "    Usage       " << opt(g.usage_percent, "%") << "\n";

        if (cfg.show_gpu_memory && g.memory_total_bytes.has_value()) {
            if (g.memory_used_bytes.has_value()) {
                const std::string type = g.memory_type.empty() ? "Memory" : g.memory_type + " Memory";
                out << "    " << utils::fit(type, 12)
                    << utils::format_bytes(g.memory_used_bytes.value())
                    << " / " << utils::format_bytes(g.memory_total_bytes.value())
                    << " (" << number(g.memory_usage_percent.value_or(0.0)) << " %)\n";
            } else {
                // No usage figure is available; report the capacity only rather
                // than deriving a plausible-looking number from system memory.
                out << "    Memory architecture: " << g.memory_type << "\n";
                out << "    System unified-memory capacity: "
                    << utils::format_bytes(g.memory_total_bytes.value()) << "\n";
            }
        }
        if (g.frequency_mhz.has_value()) {
            out << "    Frequency   " << number(g.frequency_mhz.value(), 0) << " MHz\n";
        }
        if (g.temperature_celsius.has_value()) {
            out << "    Temperature " << number(g.temperature_celsius.value()) << " °C\n";
        }
        if (g.power_watts.has_value()) {
            out << "    Power       " << number(g.power_watts.value()) << " W\n";
        }
    }
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

void TextRenderer::render_memory(std::ostream& out, const MemoryStats& s, const Config& cfg) {
    out << "Memory\n";
    out << label("RAM") << utils::format_bytes(s.ram_used_bytes) << " / "
        << utils::format_bytes(s.ram_total_bytes)
        << "  (" << number(s.ram_usage_percent) << " %)\n";
    out << label("Available") << utils::format_bytes(s.ram_available_bytes) << "\n";

    if (cfg.show_memory_cache && s.ram_cached_bytes > 0) {
        out << label("Cached") << utils::format_bytes(s.ram_cached_bytes) << "\n";
    }
    if (s.ram_buffer_bytes > 0) {
        out << label("Buffers") << utils::format_bytes(s.ram_buffer_bytes) << "\n";
    }
    if (s.active_bytes.has_value() || s.wired_bytes.has_value()) {
        out << label("Breakdown");
        bool first = true;
        auto emit = [&](const char* name, const std::optional<uint64_t>& value) {
            if (!value.has_value()) return;
            if (!first) out << "  ";
            out << name << " " << utils::format_bytes(value.value());
            first = false;
        };
        emit("active",     s.active_bytes);
        emit("inactive",   s.inactive_bytes);
        emit("wired",      s.wired_bytes);
        emit("compressed", s.compressed_bytes);
        out << "\n";
    }
    if (s.shared_bytes.has_value() || s.slab_bytes.has_value() || s.dirty_bytes.has_value()) {
        out << label("Kernel");
        bool first = true;
        auto emit = [&](const char* name, const std::optional<uint64_t>& value) {
            if (!value.has_value()) return;
            if (!first) out << "  ";
            out << name << " " << utils::format_bytes(value.value());
            first = false;
        };
        emit("shared", s.shared_bytes);
        emit("slab",   s.slab_bytes);
        emit("dirty",  s.dirty_bytes);
        out << "\n";
    }

    if (cfg.show_swap && s.swap_total_bytes > 0) {
        out << label("Swap") << utils::format_bytes(s.swap_used_bytes) << " / "
            << utils::format_bytes(s.swap_total_bytes)
            << "  (" << number(s.swap_usage_percent) << " %)\n";
    }
    if (s.commit_total_bytes.has_value() && s.commit_limit_bytes.has_value()) {
        out << label("Commit") << utils::format_bytes(s.commit_total_bytes.value()) << " / "
            << utils::format_bytes(s.commit_limit_bytes.value()) << "\n";
    }

    if (s.page_faults_per_sec.has_value() || s.page_ins_per_sec.has_value()) {
        out << label("Paging");
        bool first = true;
        auto emit = [&](const char* name, const std::optional<double>& value) {
            if (!value.has_value()) return;
            if (!first) out << "  ";
            out << name << " " << utils::format_rate(value.value(), "/s");
            first = false;
        };
        emit("faults",  s.page_faults_per_sec);
        emit("major",   s.major_faults_per_sec);
        emit("in",      s.page_ins_per_sec);
        emit("out",     s.page_outs_per_sec);
        out << "\n";
    }
    if (s.swap_ins_per_sec.has_value() && s.swap_outs_per_sec.has_value() &&
        (*s.swap_ins_per_sec > 0.0 || *s.swap_outs_per_sec > 0.0)) {
        out << label("Swapping") << "in " << utils::format_rate(*s.swap_ins_per_sec, "/s")
            << "  out " << utils::format_rate(*s.swap_outs_per_sec, "/s") << "\n";
    }
}

// ---------------------------------------------------------------------------
// Load and battery
// ---------------------------------------------------------------------------

void TextRenderer::render_load(std::ostream& out, const LoadStats& s) {
    out << "Load\n";
    out << label("1 min")  << number(s.load_1min, 2) << "\n";
    out << label("5 min")  << number(s.load_5min, 2) << "\n";
    out << label("15 min") << number(s.load_15min, 2) << "\n";
    if (s.load_per_core_1min.has_value()) {
        out << label("Per core") << number(s.load_per_core_1min.value(), 2) << "\n";
    }
    if (s.total_processes > 0) {
        out << label("Processes") << s.running_processes << " running / "
            << s.total_processes << " total";
        if (s.sleeping_processes > 0) out << ", " << s.sleeping_processes << " sleeping";
        if (s.stopped_processes > 0)  out << ", " << s.stopped_processes << " stopped";
        if (s.zombie_processes > 0)   out << ", " << s.zombie_processes << " zombie";
        out << "\n";
    }
    if (s.total_threads > 0) {
        out << label("Threads") << s.total_threads << "\n";
    }
}

void TextRenderer::render_battery(std::ostream& out, const BatteryStats& s) {
    out << "Battery / Power\n";
    out << label("Power source") << (s.ac_connected ? "AC adapter" : "Battery") << "\n";
    if (!s.state.empty())          out << label("State")   << s.state << "\n";
    if (s.percent.has_value())     out << label("Charge")  << number(s.percent.value()) << " %\n";
    if (s.time_remaining_minutes.has_value()) {
        out << label("Remaining")
            << utils::format_duration_seconds(s.time_remaining_minutes.value() * 60.0) << "\n";
    }
    if (s.health_percent.has_value()) {
        out << label("Health") << number(s.health_percent.value()) << " % of design capacity\n";
    }
    if (s.cycle_count.has_value())  out << label("Cycles")  << s.cycle_count.value() << "\n";
    if (s.voltage_volts.has_value()) out << label("Voltage") << number(s.voltage_volts.value(), 2) << " V\n";
    if (s.power_watts.has_value())  out << label("Power")   << number(s.power_watts.value(), 2) << " W\n";
    if (s.temperature_celsius.has_value()) {
        out << label("Temperature") << number(s.temperature_celsius.value()) << " °C\n";
    }
    if (s.design_capacity_mah.has_value() && s.full_capacity_mah.has_value()) {
        out << label("Capacity") << s.full_capacity_mah.value() << " / "
            << s.design_capacity_mah.value() << " mAh\n";
    }
    if (!s.technology.empty()) out << label("Technology") << s.technology << "\n";
}

// ---------------------------------------------------------------------------
// Sensors
// ---------------------------------------------------------------------------

void TextRenderer::render_temperatures(std::ostream& out, const TemperatureStats& s,
                                       const Config& cfg) {
    if (s.sensors.empty() && s.fans.empty()) return;

    if (!s.sensors.empty()) {
        out << "Temperatures\n";
        for (const auto& r : s.sensors) {
            if (cfg.excluded_sensors.count(r.name)) continue;
            out << "  " << utils::column(r.name, 28)
                << utils::fit_right(number(r.temperature_celsius) + " °C", 10);
            if (r.high.has_value()) {
                out << "  (high " << number(r.high.value(), 0) << " °C";
                if (r.critical.has_value()) {
                    out << ", crit " << number(r.critical.value(), 0) << " °C";
                }
                out << ")";
            }
            out << "\n";
        }
        out << "\n";
    }

    if (!s.fans.empty()) {
        out << "Fans\n";
        for (const auto& f : s.fans) {
            out << "  " << utils::column(f.name, 28)
                << utils::fit_right(number(f.rpm, 0) + " RPM", 10);
            if (f.max_rpm.has_value() && f.max_rpm.value() > 0) {
                out << "  (max " << number(f.max_rpm.value(), 0) << ")";
            }
            out << "\n";
        }
        out << "\n";
    }
}

// ---------------------------------------------------------------------------
// Disks
// ---------------------------------------------------------------------------

void TextRenderer::render_disks(std::ostream& out, const std::vector<DiskStats>& disks,
                                const std::vector<DiskIOStats>& io, const Config& cfg) {
    out << "Disks\n";
    out << "  " << utils::fit("MOUNTPOINT", 26) << utils::fit("FS", 8)
        << utils::fit_right("USED", 11) << utils::fit_right("SIZE", 11)
        << utils::fit_right("USE%", 8) << "  " << "INODES\n";

    for (const auto& d : disks) {
        if (d.total_bytes == 0) continue;
        if (cfg.excluded_filesystems.count(d.filesystem_type)) continue;

        out << "  " << utils::column(d.mountpoint, 26)
            << utils::column(d.filesystem_type, 8)
            << utils::fit_right(utils::format_bytes(d.used_bytes), 11)
            << utils::fit_right(utils::format_bytes(d.total_bytes), 11)
            << utils::fit_right(number(d.usage_percent) + " %", 8);
        // APFS reports a synthetic inode maximum, which makes the ratio round
        // to 0.0 % and say nothing; only show it when it is meaningful.
        if (d.inode_usage_percent.has_value() && d.inode_usage_percent.value() >= 0.1) {
            out << "  " << number(d.inode_usage_percent.value()) << " %";
        } else {
            out << "  -";
        }
        if (d.read_only) out << "  [ro]";
        out << "\n";
    }

    if (cfg.show_disk_io && !io.empty()) {
        out << "\n  Disk I/O\n";
        out << "  " << utils::fit("DEVICE", 16)
            << utils::fit_right("READ", 12) << utils::fit_right("WRITE", 12)
            << utils::fit_right("IOPS r/w", 16) << utils::fit_right("UTIL", 8) << "\n";
        for (const auto& d : io) {
            out << "  " << utils::column(d.device, 16)
                << utils::fit_right(utils::format_bytes_per_sec(d.read_bytes_per_sec), 12)
                << utils::fit_right(utils::format_bytes_per_sec(d.write_bytes_per_sec), 12)
                << utils::fit_right(number(d.read_ops_per_sec, 0) + "/" +
                                    number(d.write_ops_per_sec, 0), 16)
                << utils::fit_right(d.util_percent.has_value()
                                        ? number(d.util_percent.value(), 0) + " %" : "N/A", 8);
            if (d.avg_read_latency_ms.has_value() || d.avg_write_latency_ms.has_value()) {
                out << "  lat "
                    << (d.avg_read_latency_ms.has_value()
                            ? number(d.avg_read_latency_ms.value(), 2) : std::string("-"))
                    << "/"
                    << (d.avg_write_latency_ms.has_value()
                            ? number(d.avg_write_latency_ms.value(), 2) : std::string("-"))
                    << " ms";
            }
            out << "\n";
        }
    }
}

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------

void TextRenderer::render_network(std::ostream& out, const std::vector<NetworkStats>& net,
                                  const NetGlobalStats& global, const Config& cfg) {
    if (net.empty()) return;
    out << "Network\n";

    if (!global.default_gateway_v4.empty()) {
        out << label("Gateway") << global.default_gateway_v4;
        if (!global.default_gateway_v6.empty()) out << "  " << global.default_gateway_v6;
        out << "\n";
    }
    if (!global.dns_servers.empty()) {
        out << label("DNS");
        for (size_t i = 0; i < global.dns_servers.size(); ++i) {
            if (i > 0) out << "  ";
            out << global.dns_servers[i];
        }
        out << "\n";
    }
    if (global.total_connections > 0) {
        out << label("Sockets") << global.tcp_established << " established, "
            << global.tcp_listen << " listening, "
            << global.tcp_time_wait << " time-wait\n";
    }
    out << "\n";

    out << "  " << utils::fit("INTERFACE", 16) << utils::fit("ADDRESS", 26)
        << utils::fit_right("RX", 12) << utils::fit_right("TX", 12)
        << utils::fit_right("LINK", 12) << "  STATE\n";

    for (const auto& n : net) {
        if (cfg.excluded_interfaces.count(n.name)) continue;
        // Interfaces that have never carried a byte are noise unless asked for.
        if (!cfg.show_network_inactive && n.rx_bytes_total == 0 && n.tx_bytes_total == 0) {
            continue;
        }

        out << "  " << utils::column(n.name, 16)
            << utils::column(n.ip_address.empty() ? "-" : n.ip_address, 26)
            << utils::fit_right(utils::format_bytes_per_sec(n.rx_bytes_per_sec), 12)
            << utils::fit_right(utils::format_bytes_per_sec(n.tx_bytes_per_sec), 12)
            << utils::fit_right(n.speed_mbps.has_value()
                                    ? std::to_string(n.speed_mbps.value()) + " Mbps" : "N/A", 12)
            << "  " << (n.is_up ? "up" : "down");
        if (n.is_wireless) out << " wifi";
        out << "\n";

        if (cfg.show_network_details) {
            if (!n.mac_address.empty() || n.mtu.has_value()) {
                out << "      " << utils::fit("mac " + (n.mac_address.empty() ? "-" : n.mac_address), 28)
                    << "mtu " << (n.mtu.has_value() ? std::to_string(n.mtu.value()) : "-");
                if (!n.duplex.empty()) out << "  " << n.duplex << " duplex";
                out << "\n";
            }
            if (!n.ip6_address.empty()) {
                out << "      ipv6 " << n.ip6_address << "\n";
            }
            out << "      total " << utils::format_bytes(n.rx_bytes_total) << " in / "
                << utils::format_bytes(n.tx_bytes_total) << " out";
            if (n.rx_errors > 0 || n.tx_errors > 0 || n.rx_dropped > 0 || n.tx_dropped > 0) {
                out << "   errors " << n.rx_errors << "/" << n.tx_errors
                    << "   dropped " << n.rx_dropped << "/" << n.tx_dropped;
            }
            out << "\n";
        }
    }
}

// ---------------------------------------------------------------------------
// Connections
// ---------------------------------------------------------------------------

void TextRenderer::render_connections(std::ostream& out,
                                      const std::vector<NetConnectionStats>& conns,
                                      const Config& cfg) {
    if (conns.empty()) return;
    const size_t shown_total = std::min(static_cast<size_t>(cfg.connections_limit), conns.size());
    out << "Active Network Connections (top " << shown_total << ")\n";
    out << "  " << utils::fit("PROTO", 7) << utils::fit("LOCAL", 24)
        << utils::fit("REMOTE", 24) << utils::fit("STATE", 14)
        << utils::fit_right("PID", 8) << "  PROCESS\n";

    int shown = 0;
    for (const auto& c : conns) {
        if (++shown > cfg.connections_limit) break;

        const std::string local = c.local_addr + ":" + std::to_string(c.local_port);
        const std::string remote = (c.remote_port == 0)
                                 ? "*:*"
                                 : c.remote_addr + ":" + std::to_string(c.remote_port);

        out << "  " << utils::column(c.protocol, 7)
            << utils::column(local, 24)
            << utils::column(remote, 24)
            << utils::column(c.state, 14)
            << utils::fit_right(c.pid > 0 ? std::to_string(c.pid) : "-", 8)
            << "  " << (c.process_name.empty() ? "-" : c.process_name) << "\n";
    }
}

// ---------------------------------------------------------------------------
// Processes
// ---------------------------------------------------------------------------

void TextRenderer::render_processes(std::ostream& out, const std::vector<ProcessStats>& procs,
                                    const Config& cfg) {
    if (procs.empty()) return;
    const size_t shown_total = std::min(static_cast<size_t>(cfg.proc_limit), procs.size());
    out << "Processes (top " << shown_total << ")\n";

    out << "  " << utils::fit_right("PID", 7) << "  " << utils::fit("COMMAND", 22)
        << utils::fit("USER", 14)
        << utils::fit_right("CPU%", 8) << utils::fit_right("MEM%", 8)
        << utils::fit_right("RSS", 11) << utils::fit_right("VIRT", 11)
        << utils::fit_right("THR", 5) << utils::fit_right("TIME", 10) << "  S\n";

    int shown = 0;
    for (const auto& p : procs) {
        if (++shown > cfg.proc_limit) break;
        out << "  " << utils::fit_right(std::to_string(p.pid), 7) << "  "
            << utils::column(p.name, 22)
            << utils::column(p.user, 14)
            << utils::fit_right(number(p.cpu_percent), 8)
            << utils::fit_right(number(p.mem_percent), 8)
            << utils::fit_right(utils::format_bytes(p.mem_rss_bytes), 11)
            << utils::fit_right(utils::format_bytes(p.mem_vms_bytes), 11)
            << utils::fit_right(std::to_string(p.threads), 5)
            << utils::fit_right(utils::format_duration_seconds(p.cpu_time_seconds), 10)
            << "  " << p.state << "\n";
    }
}
