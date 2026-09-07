#include "sysmon/tui.hpp"
#include "sysmon/utils.hpp"
#include "sysmon/version.hpp"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <climits>

#include "sysmon/terminal.hpp"

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
    const terminal::Size size = terminal::size();
    term_width_  = size.width;
    term_height_ = size.height;
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
    const int visible_len = 4 + static_cast<int>(utils::display_width(title));
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

    // Compose the two halves as plain text first, so their real column widths
    // are known before any escape sequences are mixed in.  On a narrow terminal
    // the least important parts are dropped instead of running off the edge.
    const std::string brand   = std::string("⬡ sysmon v") + SYSMON_VERSION;
    const std::string os_part = sys.os + " (" + sys.architecture + ")";
    const std::string clock   = tbuf;
    const std::string uptime  = "up: " + sys.uptime;

    auto width_of = [](const std::string& text) {
        return static_cast<int>(utils::display_width(text));
    };

    // Right half, in decreasing order of importance: clock, hostname, uptime.
    std::string right_plain = clock;
    std::string right       = c_accent() + clock + reset_color() + " ";
    const int budget = width - width_of(brand) - 2;

    if (width_of(right_plain) + width_of(sys.hostname) + 2 < budget) {
        right_plain = sys.hostname + "  " + right_plain;
        right = c_dim() + sys.hostname + reset_color() + "  " +
                c_accent() + clock + reset_color() + " ";
        if (width_of(right_plain) + width_of(uptime) + 2 < budget) {
            right_plain = sys.hostname + "  " + uptime + "  " + clock;
            right = c_dim() + sys.hostname + "  " + uptime + reset_color() + "  " +
                    c_accent() + clock + reset_color() + " ";
        }
    }

    // Left half: brand always, then the OS string in whatever room is left.
    std::string left_plain = " " + brand;
    std::string left = " " + bold() + color_fg(100, 200, 255) + brand + reset_color();

    const std::string compact_tag = compact ? "  [COMPACT]" : "";
    const int room = width - width_of(left_plain) - width_of(right_plain)
                   - width_of(compact_tag) - 3;
    if (room > 8) {
        const std::string shown = utils::truncate(os_part, static_cast<size_t>(room));
        left_plain += "  " + shown;
        left += "  " + c_dim() + shown + reset_color();
    }
    if (compact) {
        left_plain += compact_tag;
        left += "  " + c_warn() + "[COMPACT]" + reset_color();
    }

    const int padding = std::max(0, width - width_of(left_plain) - width_of(right_plain) - 1);

    out << color_bg(20, 25, 35) << left
        << std::string(static_cast<size_t>(padding), ' ')
        << right << reset_color() << "\033[K\n";
}

void TUI::render_cpu_section(std::ostringstream& out, const CpuStats& cpu, int width, const Config& cfg) {
    out << section_header("CPU", width) << "\033[K\n";

    std::string cpu_bar  = progress_bar(cpu.usage_percent, std::max(12, width - 52));
    std::string spark    = c_accent() + sparkline(cpu_history_, 16) + reset_color();

    out << c_label() << " Model   " << reset_color() << c_value()
        << utils::truncate(cpu.model, static_cast<size_t>(std::max(10, width - 12)))
        << reset_color() << "\033[K\n";
    out << c_label() << " Cores   " << reset_color() << c_value()
        << cpu.logical_cores << " logical / " << cpu.physical_cores << " physical" << reset_color();

    if (cpu.frequency_mhz.has_value()) {
        out << c_dim() << "  @" << reset_color() << c_value()
            << std::fixed << std::setprecision(0) << cpu.frequency_mhz.value() << " MHz" << reset_color();
    }
    if (cpu.temperature_celsius.has_value()) {
        const double t = cpu.temperature_celsius.value();
        out << "  " << temp_color(t) << std::fixed << std::setprecision(1) << t << " °C" << reset_color();
    }
    if (!cpu.thermal_pressure.empty()) {
        // Thermal pressure is a constraint level, not a temperature.
        const std::string col = (cpu.thermal_pressure == "Nominal") ? c_good() : c_warn();
        out << c_dim() << "  thermal " << reset_color() << col << cpu.thermal_pressure << reset_color();
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
            << c_accent() << "sys " << std::fixed << std::setprecision(1) << cpu.system_percent << "%" << reset_color();
        // On a narrow terminal keep only the two states that matter most.
        if (width >= 64) {
            out << c_dim() << " | " << reset_color()
                << c_warn() << "iowait " << std::fixed << std::setprecision(1)
                << cpu.iowait_percent << "%" << reset_color()
                << c_dim() << " | idle " << std::fixed << std::setprecision(1)
                << cpu.idle_percent << "%" << reset_color();
        }
        out << "\033[K\n";
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
        if (g.gpu_cores.has_value()) {
            out << c_dim() << " (" << g.gpu_cores.value() << " GPU Cores)" << reset_color();
        }
        if (g.temperature_celsius.has_value()) {
            out << "  " << temp_color(g.temperature_celsius.value())
                << std::fixed << std::setprecision(1) << g.temperature_celsius.value() << " °C" << reset_color();
        }
        if (g.power_watts.has_value()) {
            out << c_dim() << "  " << std::fixed << std::setprecision(1) << g.power_watts.value() << " W" << reset_color();
        }
        out << "\033[K\n";

        if (g.usage_percent.has_value()) {
            out << c_label() << "  Engine " << reset_color()
                << usage_color(g.usage_percent.value())
                << std::fixed << std::setprecision(1) << g.usage_percent.value() << "%" << reset_color()
                << "  " << progress_bar(g.usage_percent.value(), std::max(10, width - 40)) << "\033[K\n";
        } else {
            out << c_label() << "  Engine " << reset_color()
                << c_dim() << "N/A" << reset_color() << "\033[K\n";
        }

        if (cfg.show_gpu_memory && g.memory_total_bytes.has_value()) {
            if (g.memory_used_bytes.has_value()) {
                std::string type_label = g.memory_type.empty() ? "VRAM" : g.memory_type + " Memory";
                out << c_label() << "  " << std::setw(10) << type_label << " " << reset_color()
                    << usage_color(g.memory_usage_percent.value_or(0.0))
                    << format_bytes(g.memory_used_bytes.value()) << " / " << format_bytes(g.memory_total_bytes.value())
                    << reset_color() << "  "
                    << progress_bar(g.memory_usage_percent.value_or(0.0), std::max(10, width - 48)) << "  "
                    << usage_color(g.memory_usage_percent.value_or(0.0))
                    << std::fixed << std::setprecision(1) << g.memory_usage_percent.value_or(0.0) << "%" << reset_color() << "\033[K\n";
            } else {
                out << c_label() << "  Memory    " << reset_color()
                    << g.memory_type << " · unified capacity "
                    << format_bytes(g.memory_total_bytes.value()) << reset_color() << "\033[K\n";
            }
        }
    }
    out << "\033[K\n";
}

void TUI::render_memory_section(std::ostringstream& out, const MemoryStats& mem, int width, const Config& cfg) {
    if (!cfg.show_memory) return;

    out << section_header("Memory (RAM & Swap)", width) << "\033[K\n";

    const double ram_pct = mem.ram_usage_percent;
    // Reserve room for the "cache: 24.0 GB" suffix that follows the bar, and
    // drop the suffix altogether when the terminal is too narrow for both.
    const bool show_cache = cfg.show_memory_cache && mem.ram_cached_bytes > 0 && width >= 72;
    const int cache_w = show_cache ? 20 : 0;
    const int bar_w   = std::max(8, width - 52 - cache_w);

    out << c_label() << " RAM   " << reset_color()
        << usage_color(ram_pct)
        << std::setw(8) << format_bytes(mem.ram_used_bytes) << " / " << format_bytes(mem.ram_total_bytes)
        << reset_color() << "  "
        << progress_bar(ram_pct, bar_w) << "  "
        << usage_color(ram_pct) << std::fixed << std::setprecision(1) << std::setw(5) << ram_pct << "%" << reset_color();
    if (show_cache) {
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
    if (load.total_processes > 0 && width >= 78) {
        out << c_dim() << "    Procs: " << reset_color() << c_value()
            << load.running_processes << " running / " << load.total_processes << " total" << reset_color();
    } else if (load.total_processes > 0) {
        out << c_dim() << "  " << reset_color() << c_value()
            << load.running_processes << "/" << load.total_processes << reset_color();
    }
    out << "\033[K\n\n";
}

void TUI::render_battery_section(std::ostringstream& out, const BatteryStats& b, int width) {
    out << section_header("Battery & Power", width) << "\033[K\n";

    const double pct = b.percent.value_or(0.0);
    out << c_label() << " Charge " << reset_color();
    if (b.percent.has_value()) {
        // Low battery is the dangerous end here, so invert the usual colouring.
        const std::string col = pct > 50.0 ? c_good() : (pct > 20.0 ? c_warn() : c_danger());
        out << col << std::fixed << std::setprecision(0) << std::setw(4) << pct << "%" << reset_color()
            << "  " << progress_bar(pct, std::max(6, width - 58));
    } else {
        out << c_dim() << "N/A" << reset_color();
    }
    out << c_dim() << "  " << (b.ac_connected ? "AC connected" : "on battery") << reset_color();
    if (!b.state.empty()) out << c_dim() << " · " << b.state << reset_color();
    out << "\033[K\n";

    // Each fact is appended only while there is room for it, so the line never
    // runs past the right edge on a narrow terminal.
    int used = 1;
    bool any_detail = false;
    std::ostringstream detail;
    auto fits = [&](const std::string& text) {
        const int cost = static_cast<int>(utils::display_width(text));
        if (used + cost > width) return false;
        used += cost;
        return true;
    };

    if (b.time_remaining_minutes.has_value() &&
        fits(" remaining " + utils::format_duration_seconds(b.time_remaining_minutes.value() * 60.0))) {
        detail << c_dim() << " remaining " << reset_color() << c_value()
               << utils::format_duration_seconds(b.time_remaining_minutes.value() * 60.0)
               << reset_color();
        any_detail = true;
    }
    if (b.health_percent.has_value() && fits("  health 100%")) {
        detail << c_dim() << "  health " << reset_color() << c_value()
               << std::fixed << std::setprecision(0) << b.health_percent.value() << "%" << reset_color();
        any_detail = true;
    }
    if (b.cycle_count.has_value() && fits("  cycles " + std::to_string(b.cycle_count.value()))) {
        detail << c_dim() << "  cycles " << reset_color() << c_value()
               << b.cycle_count.value() << reset_color();
        any_detail = true;
    }
    if (b.power_watts.has_value() && fits("  draw -00.0 W")) {
        detail << c_dim() << "  draw " << reset_color() << c_value()
               << std::fixed << std::setprecision(1) << b.power_watts.value() << " W" << reset_color();
        any_detail = true;
    }
    if (b.temperature_celsius.has_value() && fits("  temp 00.0 °C")) {
        detail << c_dim() << "  temp " << reset_color()
               << temp_color(b.temperature_celsius.value())
               << std::fixed << std::setprecision(1) << b.temperature_celsius.value() << " °C"
               << reset_color();
        any_detail = true;
    }
    if (any_detail) out << detail.str() << "\033[K\n";
    out << "\033[K\n";
}

void TUI::render_network_section(std::ostringstream& out, const std::vector<NetworkStats>& net,
                                 const NetGlobalStats& global, int width, const Config& cfg) {
    if (!cfg.show_network || net.empty()) return;

    out << section_header("Network Interfaces", width) << "\033[K\n";

    if (!global.default_gateway_v4.empty() || !global.dns_servers.empty()) {
        // Append facts only while they fit; an IPv6 nameserver alone is 39
        // columns and would otherwise push this line past the right edge.
        int used = 1;
        auto fits = [&](const std::string& text) {
            const int cost = static_cast<int>(utils::display_width(text));
            if (used + cost > width) return false;
            used += cost;
            return true;
        };

        const std::string gateway = global.default_gateway_v4.empty()
                                  ? std::string("-") : global.default_gateway_v4;
        out << c_dim() << " gateway " << reset_color() << c_value() << gateway << reset_color();
        used += 9 + static_cast<int>(utils::display_width(gateway));

        std::string dns;
        for (size_t i = 0; i < global.dns_servers.size() && i < 3; ++i) {
            const std::string candidate = dns.empty() ? global.dns_servers[i]
                                                      : dns + " " + global.dns_servers[i];
            const int cost = static_cast<int>(utils::display_width("   dns " + candidate));
            if (used + cost > width) break;
            dns = candidate;
        }
        if (!dns.empty()) {
            out << c_dim() << "   dns " << reset_color() << c_value() << dns << reset_color();
            used += static_cast<int>(utils::display_width("   dns " + dns));
        }

        if (global.total_connections > 0) {
            const std::string sockets = std::to_string(global.tcp_established) + " est / " +
                                        std::to_string(global.tcp_listen) + " listen";
            if (fits("   sockets " + sockets)) {
                out << c_dim() << "   sockets " << reset_color() << c_value()
                    << sockets << reset_color();
            }
        }
        out << "\033[K\n";
    }

    // Each row is: status + name(12) + address(17) + two rate columns (2x13)
    // + the link-speed suffix, leaving the rest to be split between the two
    // sparklines.
    // Row: 3 status + 12 name + 17 address + 2x(2 arrow + 11 rate) + up to 14
    // columns of link-speed and wifi suffix, with the remainder split between
    // the two sparklines.
    const int spark_w = std::max(0, std::min(24, (width - 78) / 2));
    for (const auto& n : net) {
        if (cfg.excluded_interfaces.count(n.name)) continue;
        if (!cfg.show_network_per_iface && n.name != "en0" && n.name != "eth0") continue;
        if (!cfg.show_network_inactive && n.rx_bytes_total == 0 && n.tx_bytes_total == 0) continue;

        // Shrink the name and address columns before letting the row overflow.
        const int name_w = (width < 62) ? 8  : 12;
        const int addr_w = (width < 62) ? 10 : 17;

        std::string status = n.is_up ? c_good() + "▲" + reset_color() : c_danger() + "▼" + reset_color();
        out << " " << status << " " << c_accent()
            << utils::column(n.name, static_cast<size_t>(name_w)) << reset_color();

        out << c_dim()
            << utils::column(n.ip_address.empty() ? "-" : n.ip_address, static_cast<size_t>(addr_w))
            << reset_color();

        out << c_good() << "↓ " << reset_color() << c_value()
            << std::right << std::setw(10) << format_bytes_per_sec(n.rx_bytes_per_sec) << reset_color();
        if (cfg.show_network_sparkline) {
            out << " " << sparkline(net_rx_history_, spark_w);
        }
        out << "  "
            << c_warn() << "↑ " << reset_color() << c_value()
            << std::right << std::setw(10) << format_bytes_per_sec(n.tx_bytes_per_sec) << reset_color();
        if (cfg.show_network_sparkline) {
            out << " " << sparkline(net_tx_history_, spark_w);
        }
        // " ▲ " (3) + name (12) + address (17) + "↓ " + 10-wide rate (12)
        // + "  ↑ " + 10-wide rate (14), plus whatever the sparklines take.
        const int row_used = 29 + name_w + addr_w
                           + 2 * (cfg.show_network_sparkline ? spark_w + 1 : 0);
        if (n.speed_mbps.has_value() && row_used + 12 <= width) {
            out << c_dim() << "  " << n.speed_mbps.value() << " Mbps" << reset_color();
        }
        if (n.is_wireless && row_used + 18 <= width) {
            out << c_dim() << " wifi" << reset_color();
        }
        out << "\033[K\n";
    }
    out << "\033[K\n";
}

void TUI::render_connections_section(std::ostringstream& out, const std::vector<NetConnectionStats>& conns, int width, const Config& cfg) {
    if (!cfg.show_connections || conns.empty() || cfg.compact_mode) return;

    out << section_header("Active Network Connections", width) << "\033[K\n";

    // Below roughly 80 columns the full table cannot fit, so drop the local
    // address and the state: the remote endpoint and the owning process are
    // what a reader is actually looking for.
    const bool narrow = width < 80;
    const int fixed = 2 + 7 + (narrow ? 0 : 14) + 7 + 2 + 12;
    const int columns = narrow ? 1 : 2;
    const int addr_w = std::max(14, std::min(24, (width - fixed) / columns));

    out << c_dim() << "  " << utils::fit("PROTO", 7);
    if (!narrow) out << utils::fit("LOCAL ADDRESS", static_cast<size_t>(addr_w));
    out << utils::fit("REMOTE ADDRESS", static_cast<size_t>(addr_w));
    if (!narrow) out << utils::fit("STATE", 14);
    out << utils::fit_right("PID", 7) << "  "
        << "PROCESS" << reset_color() << "\033[K\n";

    int shown = 0;
    for (const auto& c : conns) {
        if (++shown > cfg.connections_limit) break;

        std::string laddr = c.local_addr + ":" + std::to_string(c.local_port);
        std::string raddr = c.remote_addr + ":" + std::to_string(c.remote_port);
        if (c.remote_port == 0) raddr = "*:*";

        std::string state_col = (c.state == "ESTABLISHED") ? c_good() :
                                (c.state == "LISTEN")      ? c_accent() : c_dim();

        out << "  " << c_value() << utils::column(c.protocol, 7) << reset_color();
        if (!narrow) out << utils::column(laddr, static_cast<size_t>(addr_w));
        out << utils::column(raddr, static_cast<size_t>(addr_w));
        if (!narrow) out << state_col << utils::column(c.state, 14) << reset_color();
        if (c.pid > 0) {
            const int name_w = std::max(6, width - fixed - columns * addr_w + 12);
            out << c_dim() << utils::fit_right(std::to_string(c.pid), 7) << "  " << reset_color()
                << c_accent() << utils::truncate(c.process_name, static_cast<size_t>(name_w))
                << reset_color();
        } else {
            out << c_dim() << utils::fit_right("-", 7) << "  -" << reset_color();
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
        out << " " << c_label() << utils::column(s.name, 18) << reset_color()
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

    // 1 + name + fs(7) + 9 used + " / " + 9 total + 1 + bar + 1 + 6 percent.
    const int name_w = (width < 76) ? 14 : 22;
    const int bar_w  = std::max(6, width - 40 - name_w);
    for (const auto& d : disks) {
        if (d.total_bytes == 0) continue;
        if (cfg.excluded_filesystems.count(d.filesystem_type)) continue;

        out << " " << c_accent() << utils::column(d.mountpoint, static_cast<size_t>(name_w))
            << reset_color()
            << c_dim() << utils::column(d.filesystem_type, 7) << reset_color()
            << " " << usage_color(d.usage_percent)
            << std::setw(9) << format_bytes(d.used_bytes) << " / " << format_bytes(d.total_bytes) << reset_color()
            << " " << progress_bar(d.usage_percent, bar_w) << " "
            << usage_color(d.usage_percent)
            << std::fixed << std::setprecision(1) << std::setw(5) << d.usage_percent << "%" << reset_color() << "\033[K\n";
    }

    if (cfg.show_disk_io && !io.empty()) {
        out << "\n" << c_dim() << " Disk Throughput (Read / Write):" << reset_color() << "\033[K\n";
        for (const auto& d : io) {
            out << "  " << c_accent() << utils::column(d.device, 16) << reset_color()
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

    // 2 + 6 pid + 2 + user + 7 cpu + 9 mem + 10 rss + 6 thr + 5 state.
    const bool show_user = width >= 76;
    const int user_w = show_user ? 14 : 0;
    const int fixed  = 2 + 6 + 2 + user_w + 7 + 9 + 10 + 6 + 5;
    const int cmd_w  = std::max(10, std::min(28, width - fixed));

    out << c_dim()
        << "  " << std::right << std::setw(6) << "PID"
        << "  " << utils::fit("COMMAND", static_cast<size_t>(cmd_w))
        << utils::fit("USER", static_cast<size_t>(user_w))
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
            << "  " << c_value() << utils::column(p.name, static_cast<size_t>(cmd_w)) << reset_color()
            << c_dim()  << utils::column(p.user, static_cast<size_t>(user_w)) << reset_color()
            << cpu_col  << std::right << std::setw(7) << std::fixed << std::setprecision(1) << p.cpu_percent << reset_color()
            << mem_col  << std::setw(9) << std::fixed << std::setprecision(1) << p.mem_percent << reset_color()
            << c_value() << std::setw(10) << format_bytes(p.mem_rss_bytes) << reset_color()
            << c_dim()  << std::setw(6) << p.threads << reset_color()
            << "  " << c_accent() << p.state << reset_color() << "\033[K\n";
    }
    out << "\033[K\n";
}

void TUI::render_footer(std::ostringstream& out, int width, const Config& cfg) {
    // Build the hint text first and measure it, rather than hard-coding a
    // reserved width that silently stops matching when a key is added.
    struct Hint { const char* key; const char* label; };
    static constexpr Hint hints[] = {
        {"c", "cores"}, {"g", "gpu"},  {"n", "net"},  {"v", "conn"},
        {"p", "proc"},  {"b", "batt"}, {"o", "sort"}, {"m", "compact"},
        {"q", "quit"},
    };

    // Show the active sort order, since [o] cycles it.
    const std::string sort = "sort:" + Config::sort_name(cfg.proc_sort);

    // Keep at least a few dashes of rule, dropping hints from the right until
    // the row fits the terminal.
    const int reserve = 8;
    std::string plain;
    std::string coloured;
    for (const auto& hint : hints) {
        const std::string next = std::string("[") + hint.key + "]" + hint.label + " ";
        const int would_be = static_cast<int>(utils::display_width(plain + next + sort)) + 1;
        if (would_be > width - reserve) break;
        plain    += next;
        coloured += c_accent() + "[" + hint.key + "]" + c_dim() + hint.label + " ";
    }
    plain    += sort;
    coloured += c_value() + sort;

    const int hint_w = static_cast<int>(utils::display_width(plain)) + 1;  // + leading space

    out << c_border();
    for (int i = 0; i < std::max(0, width - hint_w); ++i) out << "─";
    out << reset_color() << " " << coloured << reset_color() << "\033[K\n";
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

void TUI::render(const Snapshot& snap, const Config& cfg) {
    const SystemStats&                     system  = snap.system;
    const CpuStats&                        cpu     = snap.cpu;
    const MemoryStats&                     memory  = snap.memory;
    const std::vector<GpuStats>&           gpus    = snap.gpus;
    const LoadStats&                       load    = snap.load;
    const std::vector<DiskStats>&          disks   = snap.disks;
    const std::vector<DiskIOStats>&        disk_io = snap.disk_io;
    const std::vector<NetworkStats>&       net     = snap.network;
    const std::vector<NetConnectionStats>& conns   = snap.connections;
    const std::vector<ProcessStats>&       procs   = snap.processes;
    const TemperatureStats&                temps   = snap.temperatures;

    update_terminal_size();
    int W = term_width_;

    // Update sparkline histories
    push_history(cpu_history_,  cpu.usage_percent);
    push_history(mem_history_,  memory.ram_usage_percent);
    if (!gpus.empty() && gpus[0].usage_percent.has_value()) {
        push_history(gpu_history_, gpus[0].usage_percent.value());
    }
    if (cpu.temperature_celsius.has_value()) {
        push_history(cpu_temp_history_, cpu.temperature_celsius.value());
    }
    double total_rx = 0, total_tx = 0;
    for (const auto& n : net) {
        if (n.name != "lo" && n.name != "lo0") {
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

    if (cfg.show_battery && snap.battery.present) {
        render_battery_section(out, snap.battery, W);
    }

    if (cfg.show_network) {
        render_network_section(out, net, snap.net_global, W, cfg);
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
