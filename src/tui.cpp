#include "sysmon/tui.hpp"
#include "sysmon/utils.hpp"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <climits>

#if defined(__unix__) || defined(__APPLE__)
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

TUI::TUI() {
    update_terminal_size();
    // Enter alternate screen buffer & hide cursor
    std::cout << "\033[?1049h\033[?25l\033[2J\033[H" << std::flush;
}

TUI::~TUI() {
    // Show cursor & leave alternate screen buffer
    std::cout << "\033[?25h\033[?1049l" << std::flush;
}

// ---------------------------------------------------------------------------
// Terminal control
// ---------------------------------------------------------------------------

void TUI::clear() {
    std::cout << "\033[2J\033[H" << std::flush;
}

void TUI::hide_cursor() {
    std::cout << "\033[?25l" << std::flush;
}

void TUI::show_cursor() {
    std::cout << "\033[?25h" << std::flush;
}

void TUI::update_terminal_size() const {
#if defined(__unix__) || defined(__APPLE__)
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        term_width_  = ws.ws_col > 0 ? ws.ws_col : 80;
        term_height_ = ws.ws_row > 0 ? ws.ws_row : 24;
    }
#endif
}

int TUI::terminal_width()  const { update_terminal_size(); return term_width_;  }
int TUI::terminal_height() const { update_terminal_size(); return term_height_; }

// ---------------------------------------------------------------------------
// ANSI helpers
// ---------------------------------------------------------------------------

std::string TUI::move_to(int row, int col) const {
    return "\033[" + std::to_string(row) + ";" + std::to_string(col) + "H";
}

std::string TUI::color_fg(int r, int g, int b) const {
    return "\033[38;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
}

std::string TUI::color_bg(int r, int g, int b) const {
    return "\033[48;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
}

std::string TUI::reset_color() const { return "\033[0m"; }
std::string TUI::bold()        const { return "\033[1m"; }
std::string TUI::dim()         const { return "\033[2m"; }

// Color theme
std::string TUI::c_title()  const { return bold() + color_fg(220, 220, 255); }
std::string TUI::c_label()  const { return color_fg(140, 160, 200); }
std::string TUI::c_value()  const { return color_fg(230, 230, 230); }
std::string TUI::c_good()   const { return color_fg( 80, 220, 120); }
std::string TUI::c_warn()   const { return color_fg(255, 200,  60); }
std::string TUI::c_danger() const { return color_fg(255,  80,  80); }
std::string TUI::c_accent() const { return color_fg(100, 180, 255); }
std::string TUI::c_border() const { return color_fg( 60,  70,  90); }
std::string TUI::c_dim()    const { return dim() + color_fg(110, 120, 140); }

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

void TUI::push_history(std::deque<double>& hist, double value) {
    hist.push_back(value);
    while (static_cast<int>(hist.size()) > HISTORY_LEN) hist.pop_front();
}

std::string TUI::format_bytes(uint64_t bytes) const {
    return utils::format_bytes(bytes);
}

std::string TUI::format_bytes_per_sec(double bps) const {
    return utils::format_bytes_per_sec(bps);
}

std::string TUI::usage_color(double pct) const {
    if (pct < 60.0) return c_good();
    if (pct < 85.0) return c_warn();
    return c_danger();
}

std::string TUI::temp_color(double celsius) const {
    if (celsius < 60.0) return c_good();
    if (celsius < 80.0) return c_warn();
    return c_danger();
}

// ---------------------------------------------------------------------------
// Widget primitives
// ---------------------------------------------------------------------------

std::string TUI::progress_bar(double pct, int width, bool colored) const {
    if (width < 4) return "";
    pct = std::max(0.0, std::min(100.0, pct));
    int filled = static_cast<int>(std::round(pct / 100.0 * (width - 2)));

    std::string bar;
    bar += c_border() + "[" + reset_color();

    std::string col = colored ? usage_color(pct) : c_accent();
    bar += col;
    for (int i = 0; i < filled; ++i)             bar += "█";
    bar += c_dim();
    for (int i = filled; i < width - 2; ++i)     bar += "░";
    bar += reset_color();
    bar += c_border() + "]" + reset_color();
    return bar;
}

std::string TUI::sparkline(const std::deque<double>& history, int width) const {
    static const char* blocks[] = {" ","▂","▃","▄","▅","▆","▇","█"};
    if (history.empty() || width <= 0) return std::string(static_cast<size_t>(width), ' ');

    double mx = *std::max_element(history.begin(), history.end());
    if (mx <= 0) mx = 100.0;

    std::string result;
    int start = static_cast<int>(history.size()) - width;
    if (start < 0) {
        result += std::string(static_cast<size_t>(-start), ' ');
        start = 0;
    }
    for (int i = start; i < static_cast<int>(history.size()); ++i) {
        double v = history[static_cast<size_t>(i)];
        int idx  = static_cast<int>(std::round(v / mx * 7.0));
        idx = std::max(0, std::min(7, idx));
        result += blocks[idx];
    }
    return result;
}

std::string TUI::section_header(const std::string& title, int width) const {
    std::string line = c_border() + "──" + reset_color() + " "
                     + c_title() + title + reset_color() + " ";
    int visible_len = 4 + static_cast<int>(title.size());
    int remaining   = width - visible_len;
    if (remaining > 0) {
        line += c_border();
        for (int i = 0; i < remaining; ++i) line += "─";
        line += reset_color();
    }
    return line;
}

std::string TUI::horizontal_rule(int width) const {
    std::string rule = c_border();
    for (int i = 0; i < width; ++i) rule += "─";
    rule += reset_color();
    return rule;
}

// ---------------------------------------------------------------------------
// Section renderers
// ---------------------------------------------------------------------------

void TUI::render_header(std::ostringstream& out, const SystemStats& sys, int width, bool compact) {
    auto t = std::time(nullptr);
    char tbuf[32];
    std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", std::localtime(&t));

    std::string left  = " " + bold() + color_fg(100,200,255) + "⬡ sysmon v0.2.0" + reset_color()
                       + "  " + c_dim() + sys.os + " (" + sys.architecture + ")" + reset_color()
                       + (compact ? ("  " + c_warn() + "[COMPACT]" + reset_color()) : "");
    std::string right = c_dim() + sys.hostname + "  up: " + sys.uptime + reset_color()
                       + "  " + c_accent() + tbuf + reset_color() + " ";

    int lvis = 20 + static_cast<int>(sys.os.size()) + static_cast<int>(sys.architecture.size()) + (compact ? 10 : 0);
    int rvis = static_cast<int>(sys.hostname.size()) + static_cast<int>(sys.uptime.size()) + 20;
    int padding = width - lvis - rvis;
    if (padding < 0) padding = 0;

    out << color_bg(20, 25, 35) << left
        << std::string(static_cast<size_t>(padding), ' ')
        << right << reset_color() << "\033[K\n";
}

void TUI::render_cpu_section(std::ostringstream& out, const CpuStats& cpu, int width, const Config& cfg) {
    out << section_header("CPU", width) << "\033[K\n";

    std::string cpu_bar  = progress_bar(cpu.usage_percent, std::max(12, width - 52));
    std::string spark    = c_accent() + sparkline(cpu_history_, 16) + reset_color();

    out << c_label() << " Model   " << reset_color() << c_value()
        << cpu.model.substr(0, static_cast<size_t>(std::max(10, width - 12))) << reset_color() << "\033[K\n";
    out << c_label() << " Cores   " << reset_color() << c_value()
        << cpu.logical_cores << " logical / " << cpu.physical_cores << " physical" << reset_color();

    if (cpu.frequency_mhz > 0) {
        out << c_dim() << "  @" << reset_color() << c_value()
            << std::fixed << std::setprecision(0) << cpu.frequency_mhz << " MHz" << reset_color();
    }
    if (cpu.temperature_celsius.has_value()) {
        double t = cpu.temperature_celsius.value();
        out << "  " << temp_color(t) << std::fixed << std::setprecision(1) << t << " °C" << reset_color();
    }
    out << "\033[K\n";

    out << c_label() << " Usage   " << reset_color()
        << usage_color(cpu.usage_percent)
        << std::fixed << std::setprecision(1) << std::setw(5) << cpu.usage_percent << "%" << reset_color()
        << "  " << cpu_bar << "  " << spark << "\033[K\n";

    if (cfg.show_cpu_cores_detail && !cfg.compact_mode) {
        out << c_label() << " Breakdown " << reset_color()
            << c_good() << "usr " << std::fixed << std::setprecision(1) << cpu.user_percent << "%" << reset_color()
            << c_dim() << " | " << reset_color()
            << c_accent() << "sys " << std::fixed << std::setprecision(1) << cpu.system_percent << "%" << reset_color()
            << c_dim() << " | " << reset_color()
            << c_warn() << "iowait " << std::fixed << std::setprecision(1) << cpu.iowait_percent << "%" << reset_color()
            << c_dim() << " | " << reset_color()
            << c_dim() << "idle " << std::fixed << std::setprecision(1) << cpu.idle_percent << "%" << reset_color() << "\033[K\n";
    }

    // Per-core bars
    if (cfg.show_cpu_per_core && !cpu.per_core.empty() && !cfg.compact_mode) {
        out << "\n" << c_dim() << " Individual Cores:" << reset_color() << "\033[K\n";
        int cores_per_row = std::max(1, width / 20);
        int col_cnt = 0;
        for (const auto& c : cpu.per_core) {
            if (col_cnt > 0 && col_cnt % cores_per_row == 0) out << "\033[K\n";
            std::ostringstream cbar;
            cbar << c_dim() << "C" << std::setw(2) << c.id << reset_color() << " "
                 << progress_bar(c.usage_percent, 8) << " "
                 << usage_color(c.usage_percent)
                 << std::setw(4) << std::fixed << std::setprecision(0) << c.usage_percent << "%" << reset_color() << " ";
            out << cbar.str();
            col_cnt++;
        }
        out << "\033[K\n";
    }
    out << "\033[K\n";
}

void TUI::render_gpu_section(std::ostringstream& out, const std::vector<GpuStats>& gpus, int width, const Config& cfg) {
    if (gpus.empty() || !cfg.show_gpu) return;

    out << section_header("GPU / Graphics", width) << "\033[K\n";

    for (const auto& g : gpus) {
        out << c_accent() << " " << g.name << reset_color()
            << c_dim() << " [" << g.vendor << "]" << reset_color();
        if (g.gpu_cores > 0) {
            out << c_dim() << " (" << g.gpu_cores << " GPU Cores)" << reset_color();
        }
        if (g.temperature_celsius.has_value()) {
            out << "  " << temp_color(g.temperature_celsius.value())
                << std::fixed << std::setprecision(1) << g.temperature_celsius.value() << " °C" << reset_color();
        }
        if (g.power_watts.has_value()) {
            out << c_dim() << "  " << std::fixed << std::setprecision(1) << g.power_watts.value() << " W" << reset_color();
        }
        out << "\033[K\n";

        if (g.usage_percent > 0.0) {
            out << c_label() << "  Engine " << reset_color()
                << usage_color(g.usage_percent)
                << std::fixed << std::setprecision(1) << g.usage_percent << "%" << reset_color()
                << "  " << progress_bar(g.usage_percent, std::max(10, width - 40)) << "\033[K\n";
        }

        if (cfg.show_gpu_memory && g.memory_total_bytes > 0) {
            std::string type_label = g.memory_type.empty() ? "VRAM" : g.memory_type + " Memory";
            out << c_label() << "  " << std::setw(10) << type_label << " " << reset_color()
                << usage_color(g.memory_usage_percent)
                << format_bytes(g.memory_used_bytes) << " / " << format_bytes(g.memory_total_bytes)
                << reset_color() << "  "
                << progress_bar(g.memory_usage_percent, std::max(10, width - 48)) << "  "
                << usage_color(g.memory_usage_percent)
                << std::fixed << std::setprecision(1) << g.memory_usage_percent << "%" << reset_color() << "\033[K\n";
        }
    }
    out << "\033[K\n";
}

void TUI::render_memory_section(std::ostringstream& out, const MemoryStats& mem, int width, const Config& cfg) {
    if (!cfg.show_memory) return;

    out << section_header("Memory (RAM & Swap)", width) << "\033[K\n";

    double ram_pct  = mem.ram_usage_percent;
    int bar_w = std::max(10, width - 48);

    out << c_label() << " RAM   " << reset_color()
        << usage_color(ram_pct)
        << std::setw(8) << format_bytes(mem.ram_used_bytes) << " / " << format_bytes(mem.ram_total_bytes)
        << reset_color() << "  "
        << progress_bar(ram_pct, bar_w) << "  "
        << usage_color(ram_pct) << std::fixed << std::setprecision(1) << std::setw(5) << ram_pct << "%" << reset_color();
    if (cfg.show_memory_cache && mem.ram_cached_bytes > 0) {
        out << c_dim() << "  cache: " << format_bytes(mem.ram_cached_bytes) << reset_color();
    }
    out << "\033[K\n";

    if (cfg.show_swap && mem.swap_total_bytes > 0) {
        double swap_pct = mem.swap_usage_percent;
        out << c_label() << " Swap  " << reset_color()
            << usage_color(swap_pct)
            << std::setw(8) << format_bytes(mem.swap_used_bytes) << " / " << format_bytes(mem.swap_total_bytes)
            << reset_color() << "  "
            << progress_bar(swap_pct, bar_w) << "  "
            << usage_color(swap_pct) << std::fixed << std::setprecision(1) << std::setw(5) << swap_pct << "%" << reset_color() << "\033[K\n";
    }
    out << "\033[K\n";
}

void TUI::render_load_section(std::ostringstream& out, const LoadStats& load, int width) {
    out << section_header("Load Average", width) << "\033[K\n";
    out << c_label() << " 1 min  " << reset_color() << c_value()
        << std::fixed << std::setprecision(2) << load.load_1min << reset_color()
        << c_dim() << "    5 min  " << reset_color() << c_value()
        << std::fixed << std::setprecision(2) << load.load_5min << reset_color()
        << c_dim() << "    15 min  " << reset_color() << c_value()
        << std::fixed << std::setprecision(2) << load.load_15min << reset_color();
    if (load.total_processes > 0) {
        out << c_dim() << "    Procs: " << reset_color() << c_value()
            << load.running_processes << " running / " << load.total_processes << " total" << reset_color();
    }
    out << "\033[K\n\n";
}

void TUI::render_network_section(std::ostringstream& out, const std::vector<NetworkStats>& net, int width, const Config& cfg) {
    if (!cfg.show_network || net.empty()) return;

    out << section_header("Network Interfaces", width) << "\033[K\n";

    int bar_w = std::max(6, (width - 60) / 2);
    for (const auto& n : net) {
        if (cfg.excluded_interfaces.count(n.interface)) continue;
        if (!cfg.show_network_per_iface && n.interface != "en0" && n.interface != "eth0") continue;

        std::string status = n.is_up ? c_good() + "▲" + reset_color() : c_danger() + "▼" + reset_color();
        out << " " << status << " " << c_accent() << std::left << std::setw(10) << n.interface << reset_color();

        if (!n.ip_address.empty()) {
            out << c_dim() << std::setw(16) << n.ip_address << reset_color() << " ";
        } else {
            out << std::setw(17) << " ";
        }

        out << c_good() << "↓ " << reset_color() << c_value()
            << std::right << std::setw(10) << format_bytes_per_sec(n.rx_bytes_per_sec) << reset_color();
        if (cfg.show_network_sparkline) {
            out << " " << sparkline(net_rx_history_, bar_w);
        }
        out << "  "
            << c_warn() << "↑ " << reset_color() << c_value()
            << std::right << std::setw(10) << format_bytes_per_sec(n.tx_bytes_per_sec) << reset_color();
        if (cfg.show_network_sparkline) {
            out << " " << sparkline(net_tx_history_, bar_w);
        }
        out << "\033[K\n";
    }
    out << "\033[K\n";
}

void TUI::render_connections_section(std::ostringstream& out, const std::vector<NetConnectionStats>& conns, int width, const Config& cfg) {
    if (!cfg.show_connections || conns.empty() || cfg.compact_mode) return;

    out << section_header("Active Network Connections", width) << "\033[K\n";

    out << c_dim()
        << "  " << std::left << std::setw(6) << "PROTO"
        << std::setw(22) << "LOCAL ADDRESS"
        << std::setw(22) << "REMOTE ADDRESS"
        << std::setw(14) << "STATE"
        << std::setw(8)  << "PID"
        << "PROCESS" << reset_color() << "\033[K\n";

    int shown = 0;
    for (const auto& c : conns) {
        if (++shown > cfg.connections_limit) break;

        std::string laddr = c.local_addr + ":" + std::to_string(c.local_port);
        std::string raddr = c.remote_addr + ":" + std::to_string(c.remote_port);
        if (c.remote_port == 0) raddr = "*:*";

        std::string state_col = (c.state == "ESTABLISHED") ? c_good() :
                                (c.state == "LISTEN")      ? c_accent() : c_dim();

        out << "  " << c_value() << std::left << std::setw(6) << c.protocol << reset_color()
            << std::setw(22) << laddr.substr(0, 21)
            << std::setw(22) << raddr.substr(0, 21)
            << state_col << std::setw(14) << c.state << reset_color();
        if (c.pid > 0) {
            out << c_dim() << std::setw(8) << c.pid << reset_color()
                << c_accent() << c.process_name << reset_color();
        } else {
            out << c_dim() << std::setw(8) << "-" << "-" << reset_color();
        }
        out << "\033[K\n";
    }
    out << "\033[K\n";
}

void TUI::render_temperature_section(std::ostringstream& out, const TemperatureStats& temps, int width, const Config& cfg) {
    if (!cfg.show_temperature || temps.sensors.empty()) return;

    out << section_header("Temperatures & Sensors", width) << "\033[K\n";

    int sensors_per_row = std::max(1, width / 28);
    int col_cnt = 0;
    for (const auto& s : temps.sensors) {
        if (cfg.excluded_sensors.count(s.name)) continue;
        if (!cfg.show_temperature_per_sensor && col_cnt >= 1) break;

        if (col_cnt > 0 && col_cnt % sensors_per_row == 0) out << "\033[K\n";
        out << " " << c_label() << std::left << std::setw(18) << s.name.substr(0, 18) << reset_color()
            << temp_color(s.temperature_celsius)
            << std::right << std::fixed << std::setprecision(1) << s.temperature_celsius << " °C"
            << reset_color() << "   ";
        col_cnt++;
    }
    out << "\033[K\n\n";
}

void TUI::render_disk_section(std::ostringstream& out, const std::vector<DiskStats>& disks,
                              const std::vector<DiskIOStats>& io, int width, const Config& cfg) {
    if (!cfg.show_disk || disks.empty()) return;

    out << section_header("Storage & Disk I/O", width) << "\033[K\n";

    int bar_w = std::max(8, width - 62);
    for (const auto& d : disks) {
        if (d.total_bytes == 0) continue;
        if (cfg.excluded_filesystems.count(d.filesystem_type)) continue;

        out << " " << c_accent() << std::left << std::setw(18) << d.mountpoint.substr(0, 18) << reset_color()
            << c_dim() << std::setw(6) << d.filesystem_type.substr(0, 6) << reset_color()
            << " " << usage_color(d.usage_percent)
            << std::setw(9) << format_bytes(d.used_bytes) << " / " << format_bytes(d.total_bytes) << reset_color()
            << " " << progress_bar(d.usage_percent, bar_w) << " "
            << usage_color(d.usage_percent)
            << std::fixed << std::setprecision(1) << std::setw(5) << d.usage_percent << "%" << reset_color() << "\033[K\n";
    }

    if (cfg.show_disk_io && !io.empty()) {
        out << "\n" << c_dim() << " Disk Throughput (Read / Write):" << reset_color() << "\033[K\n";
        for (const auto& d : io) {
            out << "  " << c_accent() << std::left << std::setw(10) << d.device << reset_color()
                << c_good() << "read: " << reset_color() << c_value() << std::setw(10) << format_bytes_per_sec(d.read_bytes_per_sec) << reset_color()
                << "  "
                << c_warn() << "write: " << reset_color() << c_value() << std::setw(10) << format_bytes_per_sec(d.write_bytes_per_sec) << reset_color() << "\033[K\n";
        }
    }
    out << "\033[K\n";
}

void TUI::render_process_section(std::ostringstream& out, const std::vector<ProcessStats>& procs, int width, const Config& cfg) {
    if (!cfg.show_processes || procs.empty() || cfg.compact_mode) return;

    out << section_header("Top Processes", width) << "\033[K\n";

    out << c_dim()
        << "  " << std::right << std::setw(6) << "PID"
        << "  " << std::left << std::setw(18) << "COMMAND"
        << std::setw(12) << "USER"
        << std::right << std::setw(7) << "CPU%"
        << std::setw(9) << "MEM%"
        << std::setw(10) << "RSS"
        << std::setw(6) << "THR"
        << "  S" << reset_color() << "\033[K\n";

    int shown = 0;
    for (const auto& p : procs) {
        if (++shown > cfg.proc_limit) break;

        std::string cpu_col = usage_color(p.cpu_percent);
        std::string mem_col = usage_color(p.mem_percent);

        out << "  " << c_dim() << std::right << std::setw(6) << p.pid << reset_color()
            << "  " << c_value() << std::left << std::setw(18) << p.name.substr(0, 18) << reset_color()
            << c_dim()  << std::setw(12) << p.user.substr(0, 12) << reset_color()
            << cpu_col  << std::right << std::setw(7) << std::fixed << std::setprecision(1) << p.cpu_percent << reset_color()
            << mem_col  << std::setw(9) << std::fixed << std::setprecision(1) << p.mem_percent << reset_color()
            << c_value() << std::setw(10) << format_bytes(p.mem_rss_bytes) << reset_color()
            << c_dim()  << std::setw(6) << p.threads << reset_color()
            << "  " << c_accent() << p.state << reset_color() << "\033[K\n";
    }
    out << "\033[K\n";
}

void TUI::render_footer(std::ostringstream& out, int width, const Config& cfg) {
    (void)cfg;
    out << c_border();
    for (int i = 0; i < std::max(0, width - 42); ++i) out << "─";
    out << reset_color()
        << " " << c_accent() << "[c]" << c_dim() << "cores "
        << c_accent() << "[g]" << c_dim() << "gpu "
        << c_accent() << "[n]" << c_dim() << "net "
        << c_accent() << "[v]" << c_dim() << "conn "
        << c_accent() << "[p]" << c_dim() << "proc "
        << c_accent() << "[m]" << c_dim() << "compact "
        << c_accent() << "[q]" << c_dim() << "quit"
        << reset_color() << "\033[K\n";
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

void TUI::render(const SystemStats&                   system,
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

    update_terminal_size();
    int W = term_width_;

    // Update sparkline histories
    push_history(cpu_history_,  cpu.usage_percent);
    push_history(mem_history_,  memory.ram_usage_percent);
    if (!gpus.empty()) {
        push_history(gpu_history_, gpus[0].usage_percent);
    }
    if (cpu.temperature_celsius.has_value()) {
        push_history(cpu_temp_history_, cpu.temperature_celsius.value());
    }
    double total_rx = 0, total_tx = 0;
    for (const auto& n : net) {
        if (n.interface != "lo" && n.interface != "lo0") {
            total_rx += n.rx_bytes_per_sec;
            total_tx += n.tx_bytes_per_sec;
        }
    }
    push_history(net_rx_history_, total_rx / 1024.0);
    push_history(net_tx_history_, total_tx / 1024.0);

    // Build the frame buffer
    std::ostringstream out;
    out << "\033[H"; // Cursor home

    render_header(out, system, W, cfg.compact_mode);

    if (cfg.show_cpu) {
        render_cpu_section(out, cpu, W, cfg);
    }

    if (cfg.show_gpu && !gpus.empty()) {
        render_gpu_section(out, gpus, W, cfg);
    }

    if (cfg.show_memory) {
        render_memory_section(out, memory, W, cfg);
    }

    render_load_section(out, load, W);

    if (cfg.show_network) {
        render_network_section(out, net, W, cfg);
    }

    if (cfg.show_connections) {
        render_connections_section(out, conns, W, cfg);
    }

    if (cfg.show_temperature) {
        render_temperature_section(out, temps, W, cfg);
    }

    if (cfg.show_disk) {
        render_disk_section(out, disks, disk_io, W, cfg);
    }

    if (cfg.show_processes) {
        render_process_section(out, procs, W, cfg);
    }

    render_footer(out, W, cfg);

    // Clear any remainder of screen to bottom
    out << "\033[J";

    // Output all at once
    std::cout << out.str() << std::flush;
}
