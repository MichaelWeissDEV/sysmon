#include "sysmon/text_renderer.hpp"
#include "sysmon/utils.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>

void TextRenderer::render(const SystemStats&                   system,
                          const CpuStats&                      cpu,
                          const MemoryStats&                   memory,
                          const std::vector<GpuStats>&         gpus,
                          const LoadStats&                     load,
                          const std::vector<DiskStats>&        disks,
                          const std::vector<DiskIOStats>&      disk_io,
                          const std::vector<NetworkStats>&     net,
                          const std::vector<NetConnectionStats>& conns,
                          const std::vector<ProcessStats>&     procs,
                          const TemperatureStats&              temps,
                          const Config&                        cfg) {
    render_system(system);
    std::cout << "\n";

    if (cfg.show_cpu) {
        render_cpu(cpu, cfg);
        std::cout << "\n";
    }

    if (cfg.show_gpu && !gpus.empty()) {
        render_gpu(gpus, cfg);
        std::cout << "\n";
    }

    if (cfg.show_memory) {
        render_memory(memory, cfg);
        std::cout << "\n";
    }

    render_load(load);
    std::cout << "\n";

    if (cfg.show_temperature) {
        render_temperatures(temps, cfg);
        std::cout << "\n";
    }

    if (cfg.show_disk) {
        render_disks(disks, disk_io, cfg);
        std::cout << "\n";
    }

    if (cfg.show_network) {
        render_network(net, cfg);
        std::cout << "\n";
    }

    if (cfg.show_connections && !conns.empty()) {
        render_connections(conns, cfg);
        std::cout << "\n";
    }

    if (cfg.show_processes && !procs.empty()) {
        render_processes(procs, cfg);
    }
}

void TextRenderer::render_system(const SystemStats& s) {
    std::cout << "System\n";
    std::cout << "  Hostname      " << s.hostname << "\n";
    std::cout << "  OS            " << s.os << "\n";
    std::cout << "  Kernel        " << s.kernel << "\n";
    std::cout << "  Architecture  " << s.architecture << "\n";
    std::cout << "  Uptime        " << s.uptime << "\n";
}

void TextRenderer::render_cpu(const CpuStats& s, const Config& cfg) {
    std::cout << "CPU\n";
    std::cout << "  Model         " << s.model << "\n";
    std::cout << "  Cores         " << s.logical_cores << " logical / " << s.physical_cores << " physical\n";
    std::cout << "  Usage         " << std::fixed << std::setprecision(1) << s.usage_percent << " %\n";
    if (cfg.show_cpu_cores_detail) {
        std::cout << "  usr/sys       " << std::fixed << std::setprecision(1) << s.user_percent
                  << " % / " << s.system_percent << " %\n";
    }
    if (s.frequency_mhz.has_value()) {
        std::cout << "  Frequency     " << std::fixed << std::setprecision(0) << s.frequency_mhz.value() << " MHz";
        if (s.max_frequency_mhz.has_value()) {
            std::cout << " / " << std::fixed << std::setprecision(0) << s.max_frequency_mhz.value() << " MHz max";
        }
        std::cout << "\n";
    } else {
        std::cout << "  Frequency     N/A\n";
    }
    if (s.temperature_celsius.has_value()) {
        std::cout << "  Temperature   " << std::fixed << std::setprecision(1) << s.temperature_celsius.value() << " °C\n";
    } else {
        std::cout << "  Temperature   N/A\n";
    }

    if (cfg.show_cpu_per_core && !s.per_core.empty()) {
        std::cout << "  Per Core:\n";
        for (const auto& c : s.per_core) {
            std::cout << "    Core " << std::setw(2) << c.id << ": "
                      << std::fixed << std::setprecision(1) << std::setw(5) << c.usage_percent << " %";
            if (c.frequency_mhz.has_value()) {
                std::cout << " (" << std::fixed << std::setprecision(0) << c.frequency_mhz.value() << " MHz)";
            }
            std::cout << "\n";
        }
    }
}

void TextRenderer::render_gpu(const std::vector<GpuStats>& gpus, const Config& cfg) {
    std::cout << "GPU / Graphics\n";
    for (const auto& g : gpus) {
        std::cout << "  " << g.name << " [" << g.vendor << "]\n";
        if (g.gpu_cores.has_value()) {
            std::cout << "    GPU Cores   " << g.gpu_cores.value() << "\n";
        }
        if (g.usage_percent.has_value()) {
            std::cout << "    Usage       " << std::fixed << std::setprecision(1) << g.usage_percent.value() << " %\n";
        } else {
            std::cout << "    Usage       N/A\n";
        }
        if (cfg.show_gpu_memory && g.memory_total_bytes.has_value()) {
            if (g.memory_used_bytes.has_value()) {
                std::string label = g.memory_type.empty() ? "Memory" : g.memory_type + " Memory";
                std::cout << "    " << label << "  " << format_bytes(g.memory_used_bytes.value())
                          << " / " << format_bytes(g.memory_total_bytes.value())
                          << " (" << std::fixed << std::setprecision(1)
                          << g.memory_usage_percent.value_or(0.0) << " %)\n";
            } else {
                std::cout << "    Memory architecture: " << g.memory_type << "\n";
                std::cout << "    System unified-memory capacity: "
                          << format_bytes(g.memory_total_bytes.value()) << "\n";
            }
        }
        if (g.frequency_mhz.has_value()) {
            std::cout << "    Frequency   " << std::fixed << std::setprecision(0)
                      << g.frequency_mhz.value() << " MHz\n";
        }
        if (g.temperature_celsius.has_value()) {
            std::cout << "    Temperature " << std::fixed << std::setprecision(1) << g.temperature_celsius.value() << " °C\n";
        }
    }
}

void TextRenderer::render_memory(const MemoryStats& s, const Config& cfg) {
    std::cout << "Memory\n";
    std::cout << "  RAM           " << format_bytes(s.ram_used_bytes) << " / " << format_bytes(s.ram_total_bytes)
              << "  (" << std::fixed << std::setprecision(1) << s.ram_usage_percent << " %)\n";
    if (cfg.show_memory_cache && s.ram_cached_bytes > 0) {
        std::cout << "  Cached        " << format_bytes(s.ram_cached_bytes) << "\n";
    }
    if (cfg.show_swap && s.swap_total_bytes > 0) {
        std::cout << "  Swap          " << format_bytes(s.swap_used_bytes) << " / " << format_bytes(s.swap_total_bytes)
                  << "  (" << std::fixed << std::setprecision(1) << s.swap_usage_percent << " %)\n";
    }
}

void TextRenderer::render_load(const LoadStats& s) {
    std::cout << "Load\n";
    std::cout << "  1 min         " << std::fixed << std::setprecision(2) << s.load_1min << "\n";
    std::cout << "  5 min         " << std::fixed << std::setprecision(2) << s.load_5min << "\n";
    std::cout << "  15 min        " << std::fixed << std::setprecision(2) << s.load_15min << "\n";
    if (s.total_processes > 0) {
        std::cout << "  Processes     " << s.running_processes << " / " << s.total_processes << "\n";
    }
}

void TextRenderer::render_temperatures(const TemperatureStats& s, const Config& cfg) {
    if (s.sensors.empty()) return;
    std::cout << "Temperatures\n";
    for (const auto& r : s.sensors) {
        if (cfg.excluded_sensors.count(r.name)) continue;
        std::cout << "  " << std::left << std::setw(20) << r.name
                  << std::right << std::setw(6) << std::fixed << std::setprecision(1)
                  << r.temperature_celsius << " °C";
        if (r.high.has_value()) {
            std::cout << "  (high: " << std::fixed << std::setprecision(0) << r.high.value() << " °C)";
        }
        std::cout << "\n";
    }
}

void TextRenderer::render_disks(const std::vector<DiskStats>& disks,
                                const std::vector<DiskIOStats>& io,
                                const Config& cfg) {
    std::cout << "Disks\n";
    for (const auto& d : disks) {
        if (d.total_bytes == 0) continue;
        if (cfg.excluded_filesystems.count(d.filesystem_type)) continue;

        std::cout << "  " << std::left << std::setw(18) << d.mountpoint
                  << format_bytes(d.used_bytes) << " / " << format_bytes(d.total_bytes)
                  << "  " << std::right << std::setw(5) << std::fixed << std::setprecision(1)
                  << d.usage_percent << " %"
                  << "  [" << d.filesystem_type << "]\n";
    }
    if (cfg.show_disk_io && !io.empty()) {
        std::cout << "\n  Disk I/O\n";
        for (const auto& d : io) {
            std::cout << "  " << std::left << std::setw(12) << d.device
                      << "  read: " << std::right << std::setw(12) << format_bps(d.read_bytes_per_sec)
                      << "  write: " << std::setw(12) << format_bps(d.write_bytes_per_sec) << "\n";
        }
    }
}

void TextRenderer::render_network(const std::vector<NetworkStats>& net, const Config& cfg) {
    if (net.empty()) return;
    std::cout << "Network\n";
    for (const auto& n : net) {
        if (cfg.excluded_interfaces.count(n.interface)) continue;
        std::cout << "  " << std::left << std::setw(12) << n.interface;
        if (!n.ip_address.empty()) {
            std::cout << std::setw(16) << n.ip_address;
        }
        std::cout << "  ↓ " << std::right << std::setw(12) << format_bps(n.rx_bytes_per_sec)
                  << "  ↑ " << std::setw(12) << format_bps(n.tx_bytes_per_sec);
        if (n.speed_mbps.has_value()) {
            std::cout << "  [" << n.speed_mbps.value() << " Mbps]";
        } else {
            std::cout << "  [link speed N/A]";
        }
        std::cout << "\n";
    }
}

void TextRenderer::render_connections(const std::vector<NetConnectionStats>& conns, const Config& cfg) {
    if (conns.empty()) return;
    std::cout << "Active Network Connections (top " << std::min(static_cast<size_t>(cfg.connections_limit), conns.size()) << ")\n";
    std::cout << "  " << std::left << std::setw(6) << "PROTO"
              << std::setw(22) << "LOCAL"
              << std::setw(22) << "REMOTE"
              << std::setw(14) << "STATE"
              << std::setw(8) << "PID"
              << "PROCESS\n";

    int shown = 0;
    for (const auto& c : conns) {
        if (++shown > cfg.connections_limit) break;
        std::string laddr = c.local_addr + ":" + std::to_string(c.local_port);
        std::string raddr = c.remote_addr + ":" + std::to_string(c.remote_port);
        if (c.remote_port == 0) raddr = "*:*";

        std::cout << "  " << std::left << std::setw(6) << c.protocol
                  << std::setw(22) << laddr.substr(0, 21)
                  << std::setw(22) << raddr.substr(0, 21)
                  << std::setw(14) << c.state;
        if (c.pid > 0) {
            std::cout << std::setw(8) << c.pid << c.process_name;
        } else {
            std::cout << std::setw(8) << "-" << "-";
        }
        std::cout << "\n";
    }
}

void TextRenderer::render_processes(const std::vector<ProcessStats>& procs, const Config& cfg) {
    if (procs.empty()) return;
    std::cout << "Processes (top " << std::min(static_cast<size_t>(cfg.proc_limit), procs.size()) << " by CPU)\n";
    std::cout << "  " << std::right << std::setw(6) << "PID"
              << "  " << std::left << std::setw(18) << "COMMAND"
              << std::setw(12) << "USER"
              << std::right << std::setw(7) << "CPU%"
              << std::setw(8) << "MEM%"
              << std::setw(10) << "RSS"
              << "\n";
    int shown = 0;
    for (const auto& p : procs) {
        if (++shown > cfg.proc_limit) break;
        std::cout << "  " << std::right << std::setw(6) << p.pid
                  << "  " << std::left << std::setw(18) << p.name.substr(0, 18)
                  << std::setw(12) << p.user.substr(0, 12)
                  << std::right << std::setw(7) << std::fixed << std::setprecision(1) << p.cpu_percent
                  << std::setw(8) << std::fixed << std::setprecision(1) << p.mem_percent
                  << std::setw(10) << format_bytes(p.mem_rss_bytes) << "\n";
    }
}

std::string TextRenderer::format_bytes(uint64_t bytes) const {
    return utils::format_bytes(bytes);
}

std::string TextRenderer::format_bps(double bps) const {
    return utils::format_bytes_per_sec(bps);
}