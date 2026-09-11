#include "sysmon/tui.hpp"
#include "sysmon/net_connections_monitor.hpp"
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
    // "── " + title + " " is 4 columns of frame, so the title itself gets
    // whatever is left.  Truncating here rather than at every call site is
    // what keeps a long, generated heading from wrapping the whole frame.
    const std::string shown = utils::truncate(title, static_cast<size_t>(std::max(1, width - 4)));

    std::string line = c_border() + "──" + reset_color() + " "
                     + c_title() + shown + reset_color() + " ";
    const int visible_len = 4 + static_cast<int>(utils::display_width(shown));
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

    // The left half is fixed cost; everything on the right is optional and is
    // added only while it still fits.  Budgeting in that order is what keeps
    // the bar inside the terminal on a narrow window — the previous version
    // computed a budget for the OS string but then appended the compact tag
    // and the right half regardless, and a 60-column terminal wrapped.
    const std::string compact_tag = compact ? "  [COMPACT]" : "";

    std::string left_plain = " " + brand + compact_tag;
    std::string left       = " " + bold() + color_fg(100, 200, 255) + brand + reset_color();
    if (compact) left += "  " + c_warn() + "[COMPACT]" + reset_color();

    // Right half, in decreasing order of importance: clock, hostname, uptime.
    // A candidate is accepted only if the whole row still fits afterwards.
    std::string right_plain;
    std::string right;
    const auto fits = [&](const std::string& candidate) {
        return width_of(left_plain) + width_of(candidate) + 2 <= width;
    };

    if (fits(clock)) {
        right_plain = clock;
        right       = c_accent() + clock + reset_color() + " ";

        const std::string with_host = sys.hostname + "  " + clock;
        if (!sys.hostname.empty() && fits(with_host)) {
            right_plain = with_host;
            right = c_dim() + sys.hostname + reset_color() + "  " +
                    c_accent() + clock + reset_color() + " ";

            const std::string with_uptime = sys.hostname + "  " + uptime + "  " + clock;
            if (fits(with_uptime)) {
                right_plain = with_uptime;
                right = c_dim() + sys.hostname + "  " + uptime + reset_color() + "  " +
                        c_accent() + clock + reset_color() + " ";
            }
        }
    }

    // The OS string takes whatever the two halves left behind.
    const int room = width - width_of(left_plain) - width_of(right_plain) - 3;
    if (room > 8) {
        const std::string shown = utils::truncate(os_part, static_cast<size_t>(room));
        left_plain += "  " + shown;
        left       += "  " + c_dim() + shown + reset_color();
    }

    const int padding = std::max(0, width - width_of(left_plain) - width_of(right_plain) - 1);

    out << color_bg(20, 25, 35) << left
        << std::string(static_cast<size_t>(padding), ' ')
        << right << reset_color() << "\033[K\n";
}

void TUI::render_cpu_section(std::ostringstream& out, const CpuStats& cpu, int width, const Config& cfg) {
    out << section_header("CPU", width) << "\033[K\n";

    // " Usage   " + "xxx.x%" + two gaps is the fixed cost; the sparkline is
    // dropped entirely before the bar is squeezed below a useful width.
    constexpr int kUsageLabel = 9;   // " Usage   "
    constexpr int kUsagePct   = 6;   // "100.0%"
    const int spark_w = (width - kUsageLabel - kUsagePct - 4 - 16 >= 8) ? 16 : 0;
    const int bar_w   = std::max(4, width - kUsageLabel - kUsagePct
                                    - (spark_w > 0 ? 4 + spark_w : 2));

    std::string cpu_bar = progress_bar(cpu.usage_percent, bar_w);
    std::string spark   = spark_w > 0
                        ? "  " + c_accent() + sparkline(cpu_history_, spark_w) + reset_color()
                        : std::string();

    out << c_label() << " Model   " << reset_color() << c_value()
        << utils::truncate(cpu.model, static_cast<size_t>(std::max(10, width - 12)))
        << reset_color() << "\033[K\n";
    // The core count is always shown; frequency, temperature and thermal state
    // are appended only while the row still fits, so a narrow terminal drops
    // the least important of them instead of wrapping.
    {
        // Even the core count can be too long for a very narrow terminal, so
        // the mandatory part is truncated rather than assumed to fit.
        std::ostringstream counts;
        counts << cpu.logical_cores << " logical / " << cpu.physical_cores << " physical";
        const std::string shown =
            utils::truncate(counts.str(), static_cast<size_t>(std::max(1, width - 9)));

        std::ostringstream plain;
        plain << " Cores   " << shown;

        out << c_label() << " Cores   " << reset_color() << c_value() << shown << reset_color();

        const auto fits = [&](const std::string& addition) {
            return static_cast<int>(utils::display_width(plain.str() + addition)) <= width;
        };

        if (cpu.frequency_mhz.has_value()) {
            std::ostringstream freq;
            freq << "  @" << std::fixed << std::setprecision(0)
                 << cpu.frequency_mhz.value() << " MHz";
            if (fits(freq.str())) {
                plain << freq.str();
                out << c_dim() << "  @" << reset_color() << c_value()
                    << std::fixed << std::setprecision(0) << cpu.frequency_mhz.value()
                    << " MHz" << reset_color();
            }
        }
        if (cpu.temperature_celsius.has_value()) {
            const double t = cpu.temperature_celsius.value();
            std::ostringstream temp;
            temp << "  " << std::fixed << std::setprecision(1) << t << " °C";
            if (fits(temp.str())) {
                plain << temp.str();
                out << "  " << temp_color(t) << std::fixed << std::setprecision(1)
                    << t << " °C" << reset_color();
            }
        }
        if (!cpu.thermal_pressure.empty()) {
            // Thermal pressure is a constraint level, not a temperature.
            const std::string addition = "  thermal " + cpu.thermal_pressure;
            if (fits(addition)) {
                const std::string col = (cpu.thermal_pressure == "Nominal") ? c_good() : c_warn();
                out << c_dim() << "  thermal " << reset_color() << col
                    << cpu.thermal_pressure << reset_color();
            }
        }
        out << "\033[K\n";
    }

    out << c_label() << " Usage   " << reset_color()
        << usage_color(cpu.usage_percent)
        << std::fixed << std::setprecision(1) << std::setw(5) << cpu.usage_percent << "%" << reset_color()
        << "  " << cpu_bar << spark << "\033[K\n";

    if (cfg.show_cpu_cores_detail && !cfg.compact_mode()) {
        // Each state is appended only while the row still fits, measured on the
        // plain text — a fixed "width >= 64" threshold said nothing about how
        // wide these particular numbers happened to be.
        const auto pct = [](double value) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << value << "%";
            return oss.str();
        };
        std::string plain = " Breakdown ";
        const auto fits = [&](const std::string& addition) {
            return static_cast<int>(utils::display_width(plain + addition)) <= width;
        };

        out << c_label() << " Breakdown " << reset_color();
        if (fits("usr " + pct(cpu.user_percent))) {
            plain += "usr " + pct(cpu.user_percent);
            out << c_good() << "usr " << pct(cpu.user_percent) << reset_color();
        }
        if (fits(" | sys " + pct(cpu.system_percent))) {
            plain += " | sys " + pct(cpu.system_percent);
            out << c_dim() << " | " << reset_color()
                << c_accent() << "sys " << pct(cpu.system_percent) << reset_color();
        }
        if (fits(" | iowait " + pct(cpu.iowait_percent))) {
            plain += " | iowait " + pct(cpu.iowait_percent);
            out << c_dim() << " | " << reset_color()
                << c_warn() << "iowait " << pct(cpu.iowait_percent) << reset_color();
        }
        if (fits(" | idle " + pct(cpu.idle_percent))) {
            out << c_dim() << " | idle " << pct(cpu.idle_percent) << reset_color();
        }
        out << "\033[K\n";
    }

    // Per-core bars
    if (cfg.show_cpu_per_core && !cpu.per_core.empty() && !cfg.compact_mode()) {
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
    if (!cfg.show_connections || conns.empty() || cfg.compact_mode()) return;

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

    const size_t conn_limit = cfg.connections_limit > 0
                            ? static_cast<size_t>(cfg.connections_limit) : conns.size();
    size_t shown = 0;
    for (const auto& c : conns) {
        if (++shown > conn_limit) break;

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

    // Every field has a reserved width, and the bar takes exactly what is
    // left.  std::setw() only pads, so a value wider than its field used to
    // push the row past the right edge of a narrow terminal.
    const int fs_w   = (width >= 68) ? 7 : 0;
    const int name_w = (width < 76) ? 14 : 22;
    const int fixed  = 1 + name_w + fs_w + 1 + 9 + 3 + 9 + 1 + 1 + 6;
    const int bar_w  = std::max(4, width - fixed);

    for (const auto& d : disks) {
        if (d.total_bytes == 0) continue;
        if (cfg.excluded_filesystems.count(d.filesystem_type)) continue;

        out << " " << c_accent() << utils::column(d.mountpoint, static_cast<size_t>(name_w))
            << reset_color();
        if (fs_w > 0) {
            out << c_dim() << utils::column(d.filesystem_type, static_cast<size_t>(fs_w))
                << reset_color();
        }
        out << " " << usage_color(d.usage_percent)
            << utils::fit_right(format_bytes(d.used_bytes), 9) << " / "
            << utils::fit_right(format_bytes(d.total_bytes), 9) << reset_color()
            << " " << progress_bar(d.usage_percent, bar_w) << " "
            << usage_color(d.usage_percent)
            << utils::fit_right(
                   utils::format_opt(std::optional<double>(d.usage_percent), "%", 1), 6)
            << reset_color() << "\033[K\n";
    }

    if (cfg.show_disk_io && !io.empty()) {
        out << "\n" << c_dim() << " Disk Throughput (Read / Write):" << reset_color() << "\033[K\n";
        // 2 margin + device + "read: " + rate + 2 + "write: " + rate.
        const int rate_w = 10;
        const int dev_w  = std::max(6, std::min(16, width - (2 + 6 + rate_w + 2 + 7 + rate_w)));
        for (const auto& d : io) {
            out << "  " << c_accent() << utils::column(d.device, static_cast<size_t>(dev_w)) << reset_color()
                << c_good() << "read: " << reset_color()
                << c_value() << utils::fit_right(format_bytes_per_sec(d.read_bytes_per_sec), rate_w) << reset_color()
                << "  "
                << c_warn() << "write: " << reset_color()
                << c_value() << utils::fit_right(format_bytes_per_sec(d.write_bytes_per_sec), rate_w) << reset_color()
                << "\033[K\n";
        }
    }
    out << "\033[K\n";
}

void TUI::render_process_section(std::ostringstream& out, const std::vector<ProcessStats>& procs, int width, const Config& cfg) {
    if (!cfg.show_processes || procs.empty() || cfg.compact_mode()) return;

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

    const size_t proc_limit = cfg.proc_limit > 0
                            ? static_cast<size_t>(cfg.proc_limit) : procs.size();
    size_t shown = 0;
    for (const auto& p : procs) {
        if (++shown > proc_limit) break;

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
    ViewState scratch;
    render(snap, cfg, scratch);
}

void TUI::render(const Snapshot& snap, const Config& cfg, ViewState& view) {
    update_terminal_size();
    const int W = term_width_;
    const int H = term_height_;

    // Sparkline histories advance once per frame regardless of the view, so
    // switching to the CPU view and back does not leave a gap in the graph.
    push_history(cpu_history_, snap.cpu.usage_percent);
    push_history(mem_history_, snap.memory.ram_usage_percent);
    if (!snap.gpus.empty() && snap.gpus[0].usage_percent.has_value()) {
        push_history(gpu_history_, snap.gpus[0].usage_percent.value());
    }
    if (snap.cpu.temperature_celsius.has_value()) {
        push_history(cpu_temp_history_, snap.cpu.temperature_celsius.value());
    }
    double total_rx = 0, total_tx = 0;
    for (const auto& n : snap.network) {
        if (!n.is_loopback) {
            total_rx += n.rx_bytes_per_sec;
            total_tx += n.tx_bytes_per_sec;
        }
    }
    push_history(net_rx_history_, total_rx / 1024.0);
    push_history(net_tx_history_, total_tx / 1024.0);

    std::ostringstream out;
    out << "\033[H";

    if (view.view != View::Overview) {
        render_view_bar(out, view, cfg, W);
    }

    switch (view.view) {
        case View::Cpu:         render_cpu_view(out, snap, cfg, view, W, H);            break;
        case View::Memory:      render_memory_view(out, snap, cfg, W);                  break;
        case View::Gpu:         render_gpu_view(out, snap, cfg, W);                     break;
        case View::Disk:        render_disk_view(out, snap, cfg, view, W, H);           break;
        case View::Network:     render_network_view(out, snap, cfg, view, W, H);        break;
        case View::Connections: render_connections_view(out, snap, cfg, view, W, H);    break;
        case View::Processes:   render_processes_view(out, snap, cfg, view, W, H);      break;
        case View::Sensors:     render_sensors_view(out, snap, cfg, view, W, H);        break;
        case View::Process:     render_process_detail_view(out, snap, cfg, view, W, H); break;
        case View::Overview:
        default:                render_overview(out, snap, cfg, W);                     break;
    }

    // The footer earns the last row unconditionally: it carries the quit key,
    // and a user on a short terminal is exactly the one who needs to find it.
    std::ostringstream footer;
    render_footer(footer, W, cfg);

    std::cout << clip_frame(out.str(), std::max(0, H - 1))
              << footer.str() << "\033[J" << std::flush;
}

void TUI::render_overview(std::ostringstream& out, const Snapshot& snap, const Config& cfg, int width) {
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

    const int W = width;

    render_header(out, system, W, cfg.compact_mode());

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
}

// ---------------------------------------------------------------------------
// Focus-view infrastructure
// ---------------------------------------------------------------------------

TUI::ListWindow TUI::clamp_window(ViewState& view, int total_rows, int viewport_rows) const {
    ListWindow w;
    w.total = std::max(0, total_rows);
    w.count = std::max(0, std::min(std::max(1, viewport_rows), w.total));

    if (w.total == 0) {
        view.cursor        = 0;
        view.scroll_offset = 0;
        return w;
    }

    // The cursor arrives from the key handler, which cannot know how long the
    // list is; [End] deliberately overshoots and settles here.
    view.cursor = std::max(0, std::min(view.cursor, w.total - 1));
    w.cursor    = view.cursor;

    // Scroll the minimum needed to keep the cursor on screen, so paging
    // through a 700-row list never jumps the viewport.
    int first = view.scroll_offset;
    if (view.cursor < first)                first = view.cursor;
    if (view.cursor >= first + w.count)     first = view.cursor - w.count + 1;
    first = std::max(0, std::min(first, w.total - w.count));

    view.scroll_offset = first;
    w.first            = first;
    return w;
}

std::string TUI::clip_frame(const std::string& frame, int rows) {
    if (rows <= 0) return "";

    int seen = 0;
    for (size_t i = 0; i < frame.size(); ++i) {
        if (frame[i] != '\n') continue;
        if (++seen == rows) return frame.substr(0, i + 1);
    }
    return frame;
}

int TUI::flex_column(int width, int fixed, std::initializer_list<int> optional,
                     int min_flex, int* taken) {
    int cost  = fixed;
    int count = 0;
    for (const int column : optional) {
        if (width - (cost + column) < min_flex) break;
        cost += column;
        ++count;
    }
    if (taken != nullptr) *taken = count;
    return std::max(1, width - cost);
}

int TUI::rows_left(const std::ostringstream& out, int height, int reserve) const {
    const std::string so_far = out.str();
    const int used = static_cast<int>(std::count(so_far.begin(), so_far.end(), '\n'));
    return std::max(1, height - used - reserve);
}

void TUI::render_view_bar(std::ostringstream& out, const ViewState& view, const Config& cfg, int width) {
    struct Tab { char key; View view; const char* label; };
    static constexpr Tab tabs[] = {
        {'0', View::Overview,    "overview"},
        {'1', View::Cpu,         "cpu"},
        {'2', View::Memory,      "memory"},
        {'3', View::Gpu,         "gpu"},
        {'4', View::Disk,        "disk"},
        {'5', View::Network,     "net"},
        {'6', View::Connections, "conn"},
        {'7', View::Processes,   "proc"},
        {'8', View::Sensors,     "sensors"},
    };

    // Build the strip a tab at a time and stop before it would wrap: a wrapped
    // header pushes every following row down by one and the frame no longer
    // lines up with the screen.
    std::string plain;
    std::string coloured;
    for (const auto& tab : tabs) {
        const bool active = (tab.view == view.view) ||
                            (tab.view == View::Processes && view.view == View::Process);
        const std::string label = std::string(1, tab.key) + ":" + tab.label + " ";
        if (static_cast<int>(utils::display_width(plain + label)) + 2 > width) break;
        plain += label;
        coloured += active
                  ? (bold() + c_accent() + std::string(1, tab.key) + ":" + tab.label + reset_color() + " ")
                  : (c_dim() + std::string(1, tab.key) + ":" + c_dim() + tab.label + reset_color() + " ");
    }

    out << " " << coloured;

    // The density indicator earns its place only if it fits.
    const std::string density = Config::detail_name(cfg.detail_level);
    const int used = static_cast<int>(utils::display_width(plain)) + 1;
    if (used + static_cast<int>(density.size()) + 3 <= width) {
        const int pad = width - used - static_cast<int>(density.size()) - 1;
        for (int i = 0; i < pad; ++i) out << " ";
        out << c_dim() << density << reset_color();
    }
    out << "\033[K\n";

    out << c_border();
    for (int i = 0; i < width; ++i) out << "─";
    out << reset_color() << "\033[K\n";
}

void TUI::render_list_status(std::ostringstream& out, const ListWindow& window,
                             const std::string& noun, const ViewState& view, int width) {
    std::ostringstream text;
    if (window.total == 0) {
        text << "no " << noun;
    } else {
        text << noun << " " << (window.first + 1) << "-" << (window.first + window.count)
             << " of " << window.total;
    }
    if (window.has_more_above()) text << "  ▲ more above";
    if (window.has_more_below()) text << "  ▼ more below";
    if (view.show_all)           text << "  [a] all";

    out << " " << c_dim() << utils::column(text.str(), static_cast<size_t>(std::max(1, width - 1)))
        << reset_color() << "\033[K\n";
}

void TUI::bar_row(std::ostringstream& out, const std::string& label, double percent, int width) {
    // 2 margin + label + bar, then " 100.0 %" costs 9 more.  Nothing here has
    // a minimum it can insist on: on a 20-column terminal a floor of 6 on both
    // the label and the bar was three columns more than the terminal had.
    const int label_w = std::min(22, std::max(1, (width - 2) / 3));
    const int rest    = std::max(1, width - 2 - label_w);
    const bool show_percent = rest >= 13;   // a bar of 4 plus the 9-column suffix
    const int bar_w   = std::max(1, rest - (show_percent ? 9 : 0));

    out << "  " << c_label() << utils::fit(label, static_cast<size_t>(label_w)) << reset_color()
        << progress_bar(percent, bar_w);
    if (show_percent) {
        out << " " << usage_color(percent) << std::fixed << std::setprecision(1)
            << std::setw(6) << percent << " %" << reset_color();
    }
    out << "\033[K\n";
}

void TUI::kv(std::ostringstream& out, const std::string& label, const std::string& value, int width) {
    if (value.empty()) return;
    // 2 columns of margin, then label and value share the rest.  The label
    // shrinks first on a narrow terminal, because a truncated label is still
    // recognisable and a truncated value is not.
    const int label_w = std::max(6, std::min(22, (width - 2) / 3));
    const int value_w = std::max(4, width - 2 - label_w);
    out << "  " << c_label() << utils::fit(label, static_cast<size_t>(label_w)) << reset_color()
        << c_value() << utils::column(value, static_cast<size_t>(value_w)) << reset_color()
        << "\033[K\n";
}

// ---------------------------------------------------------------------------
// CPU view
// ---------------------------------------------------------------------------

void TUI::render_cpu_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg,
                          ViewState& view, int width, int height) {
    const CpuStats& cpu = snap.cpu;

    out << section_header("Processor", width) << "\033[K\n";
    kv(out, "Model",        cpu.model, width);
    kv(out, "Vendor",       cpu.vendor, width);

    {
        std::ostringstream topo;
        topo << cpu.logical_cores << " logical";
        if (cpu.physical_cores > 0) topo << " / " << cpu.physical_cores << " physical";
        if (cpu.sockets.has_value()) topo << " on " << *cpu.sockets << " socket"
                                          << (*cpu.sockets == 1 ? "" : "s");
        if (cpu.threads_per_core.has_value()) topo << ", " << *cpu.threads_per_core << " thread/core";
        kv(out, "Topology", topo.str(), width);
    }

    if (cpu.performance_cores.has_value() || cpu.efficiency_cores.has_value()) {
        std::ostringstream hybrid;
        hybrid << utils::format_opt(cpu.performance_cores) << " performance, "
               << utils::format_opt(cpu.efficiency_cores)  << " efficiency";
        kv(out, "Core clusters", hybrid.str(), width);
    }

    out << "\033[K\n";

    bar_row(out, "Total", cpu.usage_percent, width);

    {
        std::ostringstream split;
        split << "usr " << std::fixed << std::setprecision(1) << cpu.user_percent
              << "  sys " << cpu.system_percent
              << "  idle " << cpu.idle_percent
              << "  iowait " << cpu.iowait_percent;
        kv(out, "Time split", split.str(), width);

        std::ostringstream rest;
        rest << "nice " << std::fixed << std::setprecision(1) << cpu.nice_percent
             << "  irq " << cpu.irq_percent
             << "  steal " << cpu.steal_percent;
        kv(out, "", rest.str(), width);
    }

    kv(out, "Frequency",     utils::format_opt(cpu.frequency_mhz, "MHz", 0), width);
    kv(out, "Freq min/base/max",
       utils::format_opt(cpu.min_frequency_mhz,  "", 0) + " / " +
       utils::format_opt(cpu.base_frequency_mhz, "", 0) + " / " +
       utils::format_opt(cpu.max_frequency_mhz,  "MHz", 0), width);
    kv(out, "Temperature",   utils::format_opt(cpu.temperature_celsius, "°C"), width);
    kv(out, "Thermal state", cpu.thermal_pressure, width);

    if (cfg.detail_level >= DetailLevel::Detailed) {
        kv(out, "Cache L1d / L1i",
           utils::format_opt_bytes(cpu.cache_l1d_bytes) + " / " +
           utils::format_opt_bytes(cpu.cache_l1i_bytes), width);
        kv(out, "Cache L2 / L3",
           utils::format_opt_bytes(cpu.cache_l2_bytes) + " / " +
           utils::format_opt_bytes(cpu.cache_l3_bytes), width);
        kv(out, "Context switches", utils::format_opt(cpu.context_switches_per_sec, "/s", 0), width);
        kv(out, "Interrupts",       utils::format_opt(cpu.interrupts_per_sec, "/s", 0), width);
        kv(out, "Forks",            utils::format_opt(cpu.forks_per_sec, "/s", 1), width);
        kv(out, "Load 1/5/15",
           utils::format_opt(std::optional<double>(snap.load.load_1min),  "", 2) + " / " +
           utils::format_opt(std::optional<double>(snap.load.load_5min),  "", 2) + " / " +
           utils::format_opt(std::optional<double>(snap.load.load_15min), "", 2), width);
        kv(out, "Load per core", utils::format_opt(snap.load.load_per_core_1min, "", 2), width);
    }

    out << "\033[K\n";
    out << " " << c_dim() << "Usage history" << reset_color() << "\033[K\n";
    out << "  " << sparkline(cpu_history_, std::max(8, std::min(width - 4, 120)))
        << "\033[K\n\033[K\n";

    // Per-core table, scrollable: a 128-thread machine does not fit a screen.
    if (!cpu.per_core.empty() && cfg.show_cpu_per_core) {
        out << section_header("Cores", width) << "\033[K\n";

        // Reserve: the column header printed just below, the list status
        // line, and the footer.
        const int viewport   = rows_left(out, height, 3);
        const ListWindow win = clamp_window(view, static_cast<int>(cpu.per_core.size()), viewport);

        const bool wide     = width >= 78;
        const int  core_bar = std::max(8, std::min(28, width - (wide ? 52 : 34)));

        out << c_dim() << "  " << utils::fit("CORE", 8)
            << utils::fit("CLUSTER", wide ? 9 : 0)
            << utils::fit_right("USE%", 7) << "  "
            << utils::fit("", static_cast<size_t>(core_bar))
            << utils::fit_right("FREQ", wide ? 10 : 0)
            << reset_color() << "\033[K\n";

        for (int i = win.first; i < win.first + win.count; ++i) {
            const CoreStats& core = cpu.per_core[static_cast<size_t>(i)];
            out << "  " << c_value() << utils::fit("#" + std::to_string(core.id), 8) << reset_color();
            if (wide) out << c_dim() << utils::fit(core.cluster.empty() ? "-" : core.cluster, 9) << reset_color();
            out << usage_color(core.usage_percent) << std::right << std::setw(6)
                << std::fixed << std::setprecision(1) << core.usage_percent << "%" << reset_color()
                << "  " << progress_bar(core.usage_percent, core_bar);
            if (wide) {
                out << " " << c_dim()
                    << utils::fit_right(utils::format_opt(core.frequency_mhz, "", 0), 9)
                    << reset_color();
            }
            out << "\033[K\n";
        }
        render_list_status(out, win, "cores", view, width);
    }

    if (cfg.detail_level >= DetailLevel::Full && !cpu.flags.empty()) {
        out << "\033[K\n" << section_header("Instruction set", width) << "\033[K\n";
        std::string line = "  ";
        for (const auto& flag : cpu.flags) {
            if (static_cast<int>(utils::display_width(line + flag)) + 1 >= width) {
                out << c_dim() << line << reset_color() << "\033[K\n";
                line = "  ";
            }
            line += flag + " ";
        }
        if (line.size() > 2) out << c_dim() << line << reset_color() << "\033[K\n";
    }
}

// ---------------------------------------------------------------------------
// Memory view
// ---------------------------------------------------------------------------

void TUI::render_memory_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg, int width) {
    const MemoryStats& m = snap.memory;

    out << section_header("Physical memory", width) << "\033[K\n";
    bar_row(out, "RAM", m.ram_usage_percent, width);

    kv(out, "Total",      format_bytes(m.ram_total_bytes), width);
    kv(out, "Used",       format_bytes(m.ram_used_bytes), width);
    kv(out, "Available",  format_bytes(m.ram_available_bytes), width);
    kv(out, "Free",       format_bytes(m.ram_free_bytes), width);
    kv(out, "Cached",     format_bytes(m.ram_cached_bytes), width);
    if (m.ram_buffer_bytes > 0) kv(out, "Buffers", format_bytes(m.ram_buffer_bytes), width);

    out << "\033[K\n" << section_header("Breakdown", width) << "\033[K\n";
    kv(out, "Active",      utils::format_opt_bytes(m.active_bytes), width);
    kv(out, "Inactive",    utils::format_opt_bytes(m.inactive_bytes), width);
    kv(out, "Wired",       utils::format_opt_bytes(m.wired_bytes), width);
    kv(out, "Compressed",  utils::format_opt_bytes(m.compressed_bytes), width);
    kv(out, "Shared",      utils::format_opt_bytes(m.shared_bytes), width);
    kv(out, "Slab",        utils::format_opt_bytes(m.slab_bytes), width);
    kv(out, "Dirty",       utils::format_opt_bytes(m.dirty_bytes), width);
    kv(out, "Pressure",    utils::format_opt(m.pressure_percent, "%"), width);

    out << "\033[K\n" << section_header("Swap & paging", width) << "\033[K\n";
    if (m.swap_total_bytes > 0) {
        bar_row(out, "Swap", m.swap_usage_percent, width);
        kv(out, "Swap used", format_bytes(m.swap_used_bytes) + " of " +
                             format_bytes(m.swap_total_bytes), width);
    } else {
        kv(out, "Swap", "not configured", width);
    }
    kv(out, "Commit charge",
       m.commit_total_bytes.has_value()
         ? utils::format_opt_bytes(m.commit_total_bytes) + " of " +
           utils::format_opt_bytes(m.commit_limit_bytes)
         : std::string(), width);
    kv(out, "Page faults",   utils::format_opt(m.page_faults_per_sec, "/s", 0), width);
    kv(out, "Major faults",  utils::format_opt(m.major_faults_per_sec, "/s", 0), width);
    kv(out, "Page in / out",
       utils::format_opt(m.page_ins_per_sec, "", 0) + " / " +
       utils::format_opt(m.page_outs_per_sec, "/s", 0), width);
    kv(out, "Swap in / out",
       utils::format_opt(m.swap_ins_per_sec, "", 0) + " / " +
       utils::format_opt(m.swap_outs_per_sec, "/s", 0), width);
    if (snap.system.page_size_bytes.has_value()) {
        kv(out, "Page size", utils::format_opt_bytes(snap.system.page_size_bytes), width);
    }

    out << "\033[K\n" << " " << c_dim() << "Usage history" << reset_color() << "\033[K\n";
    out << "  " << sparkline(mem_history_, std::max(8, std::min(width - 4, 120))) << "\033[K\n";

    // The biggest memory consumers answer "what is using my RAM", which is the
    // question that sends anyone to a memory view in the first place.
    if (cfg.detail_level >= DetailLevel::Normal && !snap.processes.empty()) {
        out << "\033[K\n" << section_header("Largest consumers", width) << "\033[K\n";
        std::vector<const ProcessStats*> by_rss;
        by_rss.reserve(snap.processes.size());
        for (const auto& p : snap.processes) by_rss.push_back(&p);
        std::stable_sort(by_rss.begin(), by_rss.end(),
                         [](const ProcessStats* a, const ProcessStats* b) {
                             return a->mem_rss_bytes > b->mem_rss_bytes;
                         });

        const int rows = cfg.detail_level >= DetailLevel::Detailed ? 12 : 6;

        // Margin, PID and gap are fixed; RSS matters most, then the percentage,
        // and VIRT is the first thing to give up on a narrow terminal.
        int columns = 0;
        const int name_w = std::min(30, flex_column(width, 2 + 7 + 2, {11, 8, 11}, 8, &columns));
        const bool show_rss  = columns >= 1;
        const bool show_pct  = columns >= 2;
        const bool show_virt = columns >= 3;

        out << c_dim() << "  " << utils::fit_right("PID", 7) << "  "
            << utils::fit("COMMAND", static_cast<size_t>(name_w))
            << utils::fit_right("RSS", show_rss ? 11 : 0)
            << utils::fit_right("VIRT", show_virt ? 11 : 0)
            << utils::fit_right("MEM%", show_pct ? 8 : 0) << reset_color() << "\033[K\n";
        for (int i = 0; i < rows && i < static_cast<int>(by_rss.size()); ++i) {
            const ProcessStats& p = *by_rss[static_cast<size_t>(i)];
            out << "  " << c_dim() << utils::fit_right(std::to_string(p.pid), 7) << reset_color()
                << "  " << c_value() << utils::column(p.name, static_cast<size_t>(name_w)) << reset_color();
            if (show_rss) {
                out << c_value() << utils::fit_right(format_bytes(p.mem_rss_bytes), 11) << reset_color();
            }
            if (show_virt) {
                out << c_dim() << utils::fit_right(format_bytes(p.mem_vms_bytes), 11) << reset_color();
            }
            if (show_pct) {
                out << usage_color(p.mem_percent) << utils::fit_right(
                           utils::format_opt(std::optional<double>(p.mem_percent), "%", 1), 8)
                    << reset_color();
            }
            out << "\033[K\n";
        }
    }
}

// ---------------------------------------------------------------------------
// GPU view
// ---------------------------------------------------------------------------

void TUI::render_gpu_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg, int width) {
    if (snap.gpus.empty()) {
        out << section_header("Graphics", width) << "\033[K\n";
        out << "  " << c_dim() << "No GPU reported by this platform." << reset_color() << "\033[K\n";
        return;
    }

    int index = 0;
    for (const auto& g : snap.gpus) {
        out << section_header("GPU " + std::to_string(index++) +
                              (g.name.empty() ? "" : " — " + g.name), width) << "\033[K\n";
        kv(out, "Vendor",   g.vendor, width);
        kv(out, "Driver",   g.driver_version, width);
        kv(out, "Cores",    utils::format_opt(g.gpu_cores), width);
        kv(out, "Memory type", g.memory_type, width);

        if (g.usage_percent.has_value()) {
            bar_row(out, "Utilisation", *g.usage_percent, width);
        } else {
            kv(out, "Utilisation", "N/A", width);
        }

        if (g.memory_usage_percent.has_value()) {
            bar_row(out, "VRAM", *g.memory_usage_percent, width);
        }

        kv(out, "VRAM total",  utils::format_opt_bytes(g.memory_total_bytes), width);
        kv(out, "VRAM used",   utils::format_opt_bytes(g.memory_used_bytes), width);
        kv(out, "VRAM free",   utils::format_opt_bytes(g.memory_free_bytes), width);

        if (cfg.detail_level >= DetailLevel::Detailed) {
            kv(out, "Core clock",   utils::format_opt(g.frequency_mhz, "MHz", 0), width);
            kv(out, "Memory clock", utils::format_opt(g.memory_frequency_mhz, "MHz", 0), width);
            kv(out, "Encoder",      utils::format_opt(g.encoder_percent, "%"), width);
            kv(out, "Decoder",      utils::format_opt(g.decoder_percent, "%"), width);
            kv(out, "Temperature",  utils::format_opt(g.temperature_celsius, "°C"), width);
            kv(out, "Power draw",   utils::format_opt(g.power_watts, "W", 2), width);
            kv(out, "Fan",          utils::format_opt(g.fan_percent, "%"), width);
        }
        out << "\033[K\n";
    }

    if (!gpu_history_.empty()) {
        out << " " << c_dim() << "Utilisation history" << reset_color() << "\033[K\n";
        out << "  " << sparkline(gpu_history_, std::max(8, std::min(width - 4, 120))) << "\033[K\n";
    }
}

// ---------------------------------------------------------------------------
// Disk view
// ---------------------------------------------------------------------------

void TUI::render_disk_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg,
                           ViewState& view, int width, int height) {
    out << section_header("Filesystems", width) << "\033[K\n";

    // Used, then the percentage, then the total, then the filesystem type and
    // inode use — dropped in that order as the terminal narrows.
    int fs_columns = 0;
    const int mount_w = std::min(34, flex_column(width, 2, {11, 8, 11, 10, 9}, 10, &fs_columns));
    const bool show_used   = fs_columns >= 1;
    const bool show_pct    = fs_columns >= 2;
    const bool show_size   = fs_columns >= 3;
    const bool show_fs     = fs_columns >= 4;
    const bool show_inodes = fs_columns >= 5;

    out << c_dim() << "  " << utils::fit("MOUNTPOINT", static_cast<size_t>(mount_w))
        << utils::fit("FS", show_fs ? 10 : 0)
        << utils::fit_right("USED", show_used ? 11 : 0)
        << utils::fit_right("SIZE", show_size ? 11 : 0)
        << utils::fit_right("USE%", show_pct ? 8 : 0)
        << utils::fit_right("INODE%", show_inodes ? 9 : 0)
        << reset_color() << "\033[K\n";

    // Disks and their I/O counters share the viewport, so the filesystem list
    // takes at most half of what is free before the I/O table needs room.
    const int fs_viewport = std::max(2, rows_left(out, height, 8) / 2);
    const ListWindow win  = clamp_window(view, static_cast<int>(snap.disks.size()), fs_viewport);

    for (int i = win.first; i < win.first + win.count; ++i) {
        const DiskStats& d = snap.disks[static_cast<size_t>(i)];
        out << "  " << c_value() << utils::column(d.mountpoint, static_cast<size_t>(mount_w)) << reset_color();
        if (show_fs) out << c_dim() << utils::column(d.filesystem_type, 10) << reset_color();
        if (show_used) {
            out << c_value() << utils::fit_right(format_bytes(d.used_bytes), 11) << reset_color();
        }
        if (show_size) {
            out << c_dim() << utils::fit_right(format_bytes(d.total_bytes), 11) << reset_color();
        }
        if (show_pct) {
            out << usage_color(d.usage_percent)
                << utils::fit_right(utils::format_opt(std::optional<double>(d.usage_percent), "%", 1), 8)
                << reset_color();
        }
        if (show_inodes) {
            out << c_dim() << utils::fit_right(utils::format_opt(d.inode_usage_percent, "%", 1), 9)
                << reset_color();
        }
        out << "\033[K\n";
    }
    render_list_status(out, win, "filesystems", view, width);

    if (cfg.detail_level >= DetailLevel::Detailed) {
        for (int i = win.first; i < win.first + win.count; ++i) {
            const DiskStats& d = snap.disks[static_cast<size_t>(i)];
            std::ostringstream detail;
            detail << d.device;
            if (!d.mount_options.empty()) detail << "  [" << d.mount_options << "]";
            if (d.read_only) detail << "  read-only";
            if (d.removable) detail << "  removable";
            if (d.inodes_total.has_value()) {
                detail << "  inodes " << utils::format_opt(d.inodes_used)
                       << " / " << utils::format_opt(d.inodes_total);
            }
            out << "    " << c_dim()
                << utils::column(d.mountpoint + ": " + detail.str(),
                                 static_cast<size_t>(std::max(1, width - 5)))
                << reset_color() << "\033[K\n";
        }
    }

    out << "\033[K\n" << section_header("Device I/O", width) << "\033[K\n";
    if (snap.disk_io.empty()) {
        out << "  " << c_dim() << "No per-device counters available." << reset_color() << "\033[K\n";
        return;
    }

    // Read and write rates first, then IOPS, then the timing columns.
    int io_columns = 0;
    const int dev_w = std::min(20, flex_column(width, 2, {12, 12, 14, 8, 14}, 6, &io_columns));
    const bool show_read  = io_columns >= 1;
    const bool show_write = io_columns >= 2;
    const bool show_iops  = io_columns >= 3;
    const bool show_util  = io_columns >= 4;
    const bool show_lat   = io_columns >= 5;

    out << c_dim() << "  " << utils::fit("DEVICE", static_cast<size_t>(dev_w))
        << utils::fit_right("READ", show_read ? 12 : 0)
        << utils::fit_right("WRITE", show_write ? 12 : 0)
        << utils::fit_right("IOPS r/w", show_iops ? 14 : 0)
        << utils::fit_right("UTIL", show_util ? 8 : 0)
        << utils::fit_right("LAT r/w", show_lat ? 14 : 0)
        << reset_color() << "\033[K\n";

    for (const auto& io : snap.disk_io) {
        out << "  " << c_value() << utils::column(io.device, static_cast<size_t>(dev_w)) << reset_color();
        if (show_read) {
            out << c_good() << utils::fit_right(format_bytes_per_sec(io.read_bytes_per_sec), 12) << reset_color();
        }
        if (show_write) {
            out << c_warn() << utils::fit_right(format_bytes_per_sec(io.write_bytes_per_sec), 12) << reset_color();
        }
        if (show_iops) {
            std::ostringstream iops;
            iops << std::fixed << std::setprecision(0)
                 << io.read_ops_per_sec << "/" << io.write_ops_per_sec;
            out << c_dim() << utils::fit_right(iops.str(), 14) << reset_color();
        }
        if (show_util) {
            out << c_dim() << utils::fit_right(utils::format_opt(io.util_percent, "%", 0), 8) << reset_color();
        }
        if (show_lat) {
            std::ostringstream lat;
            lat << utils::format_opt(io.avg_read_latency_ms, "", 1) << "/"
                << utils::format_opt(io.avg_write_latency_ms, "ms", 1);
            out << c_dim() << utils::fit_right(lat.str(), 14) << reset_color();
        }
        out << "\033[K\n";
    }

    if (cfg.detail_level >= DetailLevel::Detailed) {
        out << "\033[K\n";
        for (const auto& io : snap.disk_io) {
            std::ostringstream totals;
            totals << io.device << ": read " << format_bytes(io.read_bytes_total)
                   << " in " << io.read_ops_total << " ops, written "
                   << format_bytes(io.write_bytes_total)
                   << " in " << io.write_ops_total << " ops";
            if (io.queue_depth.has_value()) {
                totals << ", queue " << utils::format_opt(io.queue_depth, "", 2);
            }
            out << "    " << c_dim()
                << utils::column(totals.str(), static_cast<size_t>(std::max(1, width - 5)))
                << reset_color() << "\033[K\n";
        }
    }

    // Which programs are actually touching the disk right now — the question
    // that a device-level throughput number cannot answer.
    if (cfg.detail_level >= DetailLevel::Normal && !snap.processes.empty()) {
        // Current rate answers "what is hammering the disk right now"; the
        // lifetime totals answer "how much has this program written", which on
        // an idle machine is the only one of the two with anything in it.
        const auto rate_of = [](const ProcessStats& p) {
            return p.io_read_bytes_per_sec.value_or(0.0) +
                   p.io_write_bytes_per_sec.value_or(0.0);
        };
        const auto total_of = [](const ProcessStats& p) {
            return p.io_read_bytes_total.value_or(0) + p.io_write_bytes_total.value_or(0);
        };

        std::vector<const ProcessStats*> writers;
        for (const auto& p : snap.processes) {
            if (rate_of(p) > 0.0 || total_of(p) > 0) writers.push_back(&p);
        }

        out << "\033[K\n" << section_header("Disk use by process", width) << "\033[K\n";
        if (writers.empty()) {
            out << "  " << c_dim()
                << utils::column("No per-process disk counters are readable here.",
                                 static_cast<size_t>(std::max(1, width - 3)))
                << reset_color() << "\033[K\n";
        } else {
            std::stable_sort(writers.begin(), writers.end(),
                             [&](const ProcessStats* a, const ProcessStats* b) {
                                 if (rate_of(*a) != rate_of(*b)) return rate_of(*a) > rate_of(*b);
                                 return total_of(*a) > total_of(*b);
                             });

            int io_cols = 0;
            const int name_w = std::min(30, flex_column(width, 2 + 7 + 2, {12, 12, 12, 12}, 8, &io_cols));
            const bool proc_write  = io_cols >= 1;
            const bool proc_read   = io_cols >= 2;
            const bool proc_wtotal = io_cols >= 3;
            const bool proc_rtotal = io_cols >= 4;

            out << c_dim() << "  " << utils::fit_right("PID", 7) << "  "
                << utils::fit("COMMAND", static_cast<size_t>(name_w))
                << utils::fit_right("READ/s", proc_read ? 12 : 0)
                << utils::fit_right("WRITE/s", proc_write ? 12 : 0)
                << utils::fit_right("READ TOTAL", proc_rtotal ? 12 : 0)
                << utils::fit_right("WRITE TOTAL", proc_wtotal ? 12 : 0)
                << reset_color() << "\033[K\n";

            const int rows = cfg.detail_level >= DetailLevel::Detailed ? 10 : 5;
            for (int i = 0; i < rows && i < static_cast<int>(writers.size()); ++i) {
                const ProcessStats& p = *writers[static_cast<size_t>(i)];
                out << "  " << c_dim() << utils::fit_right(std::to_string(p.pid), 7) << reset_color()
                    << "  " << c_value() << utils::column(p.name, static_cast<size_t>(name_w)) << reset_color();
                if (proc_read) {
                    out << c_good() << utils::fit_right(utils::format_opt_rate(p.io_read_bytes_per_sec), 12)
                        << reset_color();
                }
                if (proc_write) {
                    out << c_warn() << utils::fit_right(utils::format_opt_rate(p.io_write_bytes_per_sec), 12)
                        << reset_color();
                }
                if (proc_rtotal) {
                    out << c_dim() << utils::fit_right(utils::format_opt_bytes(p.io_read_bytes_total), 12)
                        << reset_color();
                }
                if (proc_wtotal) {
                    out << c_dim() << utils::fit_right(utils::format_opt_bytes(p.io_write_bytes_total), 12)
                        << reset_color();
                }
                out << "\033[K\n";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Network view
// ---------------------------------------------------------------------------

void TUI::render_network_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg,
                              ViewState& view, int width, int height) {
    const NetGlobalStats& g = snap.net_global;

    out << section_header("Network", width) << "\033[K\n";
    kv(out, "Default gateway", g.default_gateway_v4, width);
    kv(out, "Gateway (IPv6)",  g.default_gateway_v6, width);
    {
        std::string dns;
        for (const auto& server : g.dns_servers) {
            if (!dns.empty()) dns += "  ";
            dns += server;
        }
        kv(out, "DNS servers", dns, width);
    }
    kv(out, "Search domain", g.domain, width);
    {
        std::ostringstream sockets;
        sockets << g.tcp_established << " established, " << g.tcp_listen << " listening, "
                << g.tcp_time_wait << " time-wait, " << g.udp_sockets << " udp";
        kv(out, "Sockets", sockets.str(), width);
    }

    out << "\033[K\n";
    const int spark_w = std::max(8, std::min(width - 12, 100));
    out << "  " << c_good() << utils::fit("RX", 6) << reset_color()
        << sparkline(net_rx_history_, spark_w) << "\033[K\n";
    out << "  " << c_warn() << utils::fit("TX", 6) << reset_color()
        << sparkline(net_tx_history_, spark_w) << "\033[K\n";

    out << "\033[K\n" << section_header("Interfaces", width) << "\033[K\n";

    // Hide interfaces that never carried traffic unless asked, otherwise a Mac
    // buries the real interface under twenty utun/bridge entries.
    std::vector<const NetworkStats*> shown;
    for (const auto& n : snap.network) {
        if (cfg.excluded_interfaces.count(n.name) > 0) continue;
        const bool idle = n.rx_bytes_total == 0 && n.tx_bytes_total == 0;
        if (idle && !cfg.show_network_inactive && !view.show_all) continue;
        shown.push_back(&n);
    }

    // The interface name is what a row is useless without, so it is the last
    // thing to shrink; the address, the two rates, the state and the link
    // speed are given up in that order as the terminal narrows.
    int net_columns = 0;
    const int name_flex = flex_column(width, 2, {12, 12, 2 + 6, 11}, 6, &net_columns);
    const bool show_rates = net_columns >= 2;
    const bool show_state = net_columns >= 3;
    const bool show_link  = net_columns >= 4;

    // The name takes a third of the shared space, capped at 16, and never
    // more than the space actually is; the address gets the remainder.
    const int name_w = std::min(name_flex, std::min(16, std::max(6, name_flex / 3)));
    const int addr_w = std::max(0, name_flex - name_w);

    out << c_dim() << "  " << utils::fit("INTERFACE", static_cast<size_t>(name_w))
        << utils::fit("ADDRESS", static_cast<size_t>(addr_w))
        << utils::fit_right("RX", show_rates ? 12 : 0)
        << utils::fit_right("TX", show_rates ? 12 : 0)
        << utils::fit_right("LINK", show_link ? 11 : 0)
        << (show_state ? "  " : "") << utils::fit("STATE", show_state ? 6 : 0)
        << reset_color() << "\033[K\n";

    // The detailed level prints two extra lines under each interface, and the
    // bandwidth table below needs room of its own.
    int viewport = rows_left(out, height, 12);
    if (cfg.detail_level >= DetailLevel::Detailed) viewport /= 3;
    const ListWindow win = clamp_window(view, static_cast<int>(shown.size()), std::max(1, viewport));

    for (int i = win.first; i < win.first + win.count; ++i) {
        const NetworkStats& n = *shown[static_cast<size_t>(i)];
        out << "  " << c_value() << utils::column(n.name, static_cast<size_t>(name_w)) << reset_color()
            << c_dim()  << utils::column(n.ip_address.empty() ? "-" : n.ip_address,
                                         static_cast<size_t>(addr_w)) << reset_color();
        if (show_rates) {
            out << c_good() << utils::fit_right(format_bytes_per_sec(n.rx_bytes_per_sec), 12) << reset_color()
                << c_warn() << utils::fit_right(format_bytes_per_sec(n.tx_bytes_per_sec), 12) << reset_color();
        }
        if (show_link) {
            out << c_dim() << utils::fit_right(utils::format_opt(n.speed_mbps, "Mbps"), 11) << reset_color();
        }
        if (show_state) {
            out << "  " << (n.is_up ? c_good() : c_dim()) << utils::fit(n.is_up ? "up" : "down", 6)
                << reset_color();
        }
        out << "\033[K\n";

        if (cfg.detail_level >= DetailLevel::Detailed) {
            std::ostringstream detail;
            detail << "MAC " << (n.mac_address.empty() ? "N/A" : n.mac_address)
                   << "  MTU " << utils::format_opt(n.mtu)
                   << "  " << (n.duplex.empty() ? "duplex N/A" : n.duplex + " duplex");
            if (n.is_wireless) detail << "  wireless";
            if (!n.ip6_address.empty()) detail << "  " << n.ip6_address;
            out << "    " << c_dim()
                << utils::column(detail.str(), static_cast<size_t>(std::max(1, width - 5)))
                << reset_color() << "\033[K\n";

            std::ostringstream counters;
            counters << "total rx " << format_bytes(n.rx_bytes_total)
                     << " / tx " << format_bytes(n.tx_bytes_total)
                     << "  packets " << n.rx_packets_total << "/" << n.tx_packets_total
                     << "  errors " << n.rx_errors << "/" << n.tx_errors
                     << "  dropped " << n.rx_dropped << "/" << n.tx_dropped;
            out << "    " << c_dim()
                << utils::column(counters.str(), static_cast<size_t>(std::max(1, width - 5)))
                << reset_color() << "\033[K\n";
        }
    }
    render_list_status(out, win, "interfaces", view, width);

    // Per-process bandwidth, where the platform can attribute it.
    if (cfg.detail_level >= DetailLevel::Normal) {
        std::vector<const ProcessStats*> talkers;
        for (const auto& p : snap.processes) {
            if (p.tx_bytes_per_sec.value_or(0.0) > 0.0) talkers.push_back(&p);
        }
        out << "\033[K\n" << section_header("Upload by process", width) << "\033[K\n";
        if (talkers.empty()) {
            out << "  " << c_dim()
                << utils::column(NetConnectionsMonitor::bandwidth_attribution_supported()
                                   ? "No process has sent measurable traffic since the last refresh."
                                   : "This platform exposes no per-process byte counters to an "
                                     "unprivileged process, so sysmon reports N/A rather than a guess.",
                                 static_cast<size_t>(std::max(1, width - 3)))
                << reset_color() << "\033[K\n";
        } else {
            std::stable_sort(talkers.begin(), talkers.end(),
                             [](const ProcessStats* a, const ProcessStats* b) {
                                 return a->tx_bytes_per_sec.value_or(0.0) >
                                        b->tx_bytes_per_sec.value_or(0.0);
                             });
            const int proc_w = std::max(10, std::min(30, width - 34));
            out << c_dim() << "  " << utils::fit_right("PID", 7) << "  "
                << utils::fit("COMMAND", static_cast<size_t>(proc_w))
                << utils::fit_right("TX", 12) << utils::fit_right("SOCKETS", 9)
                << reset_color() << "\033[K\n";
            const int rows = cfg.detail_level >= DetailLevel::Detailed ? 12 : 6;
            for (int i = 0; i < rows && i < static_cast<int>(talkers.size()); ++i) {
                const ProcessStats& p = *talkers[static_cast<size_t>(i)];
                out << "  " << c_dim() << utils::fit_right(std::to_string(p.pid), 7) << reset_color()
                    << "  " << c_value() << utils::column(p.name, static_cast<size_t>(proc_w)) << reset_color()
                    << c_warn() << utils::fit_right(utils::format_opt_rate(p.tx_bytes_per_sec), 12) << reset_color()
                    << c_dim() << utils::fit_right(utils::format_opt(p.socket_count), 9) << reset_color()
                    << "\033[K\n";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Connections view
// ---------------------------------------------------------------------------

void TUI::render_connections_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg,
                                  ViewState& view, int width, int height) {
    out << section_header("Connections", width) << "\033[K\n";

    const std::vector<NetConnectionStats>& conns = snap.connections;

    // Margin, protocol and the gap are fixed; state and PID are given up in
    // that order; the two addresses and the process name share the rest, and
    // all three shrink together so the row cannot exceed the terminal.
    int conn_columns = 0;
    const int shared = flex_column(width, 2 /*margin*/ + 7 /*protocol*/ + 2 /*gap*/,
                                   {13 /*state*/, 8 /*pid*/}, 22, &conn_columns);
    const bool show_state = conn_columns >= 1;
    const bool show_pid   = conn_columns >= 2;

    // Two addresses take two fifths each; the process name gets the remainder,
    // and neither claims a minimum the terminal cannot pay.
    const int addr_w = std::max(1, std::min(24, shared * 2 / 5));
    const int proc_w = std::max(0, std::min(24, shared - 2 * addr_w));

    out << c_dim() << "  " << utils::fit("PROTO", 7)
        << utils::fit("LOCAL", static_cast<size_t>(addr_w))
        << utils::fit("REMOTE", static_cast<size_t>(addr_w))
        << utils::fit("STATE", show_state ? 13 : 0)
        << utils::fit_right("PID", show_pid ? 8 : 0)
        << "  " << utils::fit("PROCESS", static_cast<size_t>(proc_w))
        << reset_color() << "\033[K\n";

    const int viewport = rows_left(out, height, 3);
    const ListWindow win = clamp_window(view, static_cast<int>(conns.size()), viewport);

    for (int i = win.first; i < win.first + win.count; ++i) {
        const NetConnectionStats& c = conns[static_cast<size_t>(i)];
        const std::string local  = c.local_addr  + ":" + std::to_string(c.local_port);
        const std::string remote = c.remote_port == 0 ? "-"
                                 : c.remote_addr + ":" + std::to_string(c.remote_port);

        std::string state_color = c_dim();
        if (c.state == "ESTABLISHED") state_color = c_good();
        else if (c.state == "LISTEN") state_color = c_accent();

        out << "  " << c_dim()  << utils::column(c.protocol, 7) << reset_color()
            << c_value() << utils::column(local,  static_cast<size_t>(addr_w)) << reset_color()
            << c_value() << utils::column(remote, static_cast<size_t>(addr_w)) << reset_color();
        if (show_state) out << state_color << utils::column(c.state, 13) << reset_color();
        if (show_pid) {
            out << c_dim() << utils::fit_right(c.pid > 0 ? std::to_string(c.pid) : "-", 8) << reset_color();
        }
        out << "  " << c_value()
            << utils::column(c.process_name.empty() ? "-" : c.process_name, static_cast<size_t>(proc_w))
            << reset_color() << "\033[K\n";
    }
    render_list_status(out, win, "connections", view, width);

    if (!cfg.connections_show_listen && !view.show_all) {
        out << " " << c_dim()
            << utils::column("Listening sockets are hidden; press [a] or start with --listen.",
                             static_cast<size_t>(std::max(1, width - 1)))
            << reset_color() << "\033[K\n";
    }
}

// ---------------------------------------------------------------------------
// Sensors view
// ---------------------------------------------------------------------------

void TUI::render_sensors_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg,
                              ViewState& view, int width, int height) {
    const TemperatureStats& t = snap.temperatures;

    out << section_header("Sensors", width) << "\033[K\n";
    kv(out, "CPU package", utils::format_opt(t.cpu_package, "°C"), width);
    kv(out, "Hottest",
       t.hottest_celsius.has_value()
         ? utils::format_opt(t.hottest_celsius, "°C") +
           (t.hottest_name.empty() ? "" : "  (" + t.hottest_name + ")")
         : std::string("N/A"), width);
    if (!snap.cpu.thermal_pressure.empty()) {
        kv(out, "Thermal pressure", snap.cpu.thermal_pressure, width);
    }

    out << "\033[K\n";
    if (t.sensors.empty()) {
        out << "  " << c_dim()
            << utils::column("No temperature sensors are readable without privileges here.",
                             static_cast<size_t>(std::max(1, width - 3)))
            << reset_color() << "\033[K\n";
    } else {
        const bool wide   = width >= 84;
        const int  name_w = std::max(12, std::min(32, width - (wide ? 52 : 34)));
        out << c_dim() << "  " << utils::fit("SENSOR", static_cast<size_t>(name_w))
            << utils::fit("CHIP", wide ? 16 : 0)
            << utils::fit_right("TEMP", 10)
            << utils::fit_right("HIGH", wide ? 9 : 0)
            << utils::fit_right("CRIT", wide ? 9 : 0)
            << reset_color() << "\033[K\n";

        // The fan and battery sections below still need their rows.
        const int reserve    = 4 + static_cast<int>(t.fans.size()) +
                               (snap.battery.present ? 10 : 0);
        const int viewport   = rows_left(out, height, reserve);
        const ListWindow win = clamp_window(view, static_cast<int>(t.sensors.size()), viewport);

        for (int i = win.first; i < win.first + win.count; ++i) {
            const SensorReading& s = t.sensors[static_cast<size_t>(i)];
            out << "  " << c_value() << utils::column(s.name, static_cast<size_t>(name_w)) << reset_color();
            if (wide) out << c_dim() << utils::column(s.chip, 16) << reset_color();
            out << temp_color(s.temperature_celsius)
                << utils::fit_right(
                       utils::format_opt(std::optional<double>(s.temperature_celsius), "°C", 1), 10)
                << reset_color();
            if (wide) {
                out << c_dim() << utils::fit_right(utils::format_opt(s.high, "", 0), 9)
                    << utils::fit_right(utils::format_opt(s.critical, "", 0), 9) << reset_color();
            }
            out << "\033[K\n";
        }
        render_list_status(out, win, "sensors", view, width);
    }

    out << "\033[K\n" << section_header("Fans", width) << "\033[K\n";
    if (t.fans.empty()) {
        out << "  " << c_dim()
            << utils::column("No fan tachometers exposed on this platform.",
                             static_cast<size_t>(std::max(1, width - 3)))
            << reset_color() << "\033[K\n";
    } else {
        for (const auto& f : t.fans) {
            std::ostringstream value;
            value << std::fixed << std::setprecision(0) << f.rpm << " rpm";
            if (f.min_rpm.has_value() || f.max_rpm.has_value()) {
                value << "  (" << utils::format_opt(f.min_rpm, "", 0) << " - "
                      << utils::format_opt(f.max_rpm, "", 0) << ")";
            }
            kv(out, f.name.empty() ? "fan" : f.name, value.str(), width);
        }
    }

    if (snap.battery.present) {
        out << "\033[K\n" << section_header("Battery", width) << "\033[K\n";
        kv(out, "Charge",       utils::format_opt(snap.battery.percent, "%"), width);
        kv(out, "State",        snap.battery.state, width);
        kv(out, "Health",       utils::format_opt(snap.battery.health_percent, "%"), width);
        kv(out, "Cycles",       utils::format_opt(snap.battery.cycle_count), width);
        kv(out, "Temperature",  utils::format_opt(snap.battery.temperature_celsius, "°C"), width);
        kv(out, "Voltage",      utils::format_opt(snap.battery.voltage_volts, "V", 3), width);
        kv(out, "Power",        utils::format_opt(snap.battery.power_watts, "W", 2), width);
        if (cfg.detail_level >= DetailLevel::Detailed) {
            kv(out, "Capacity design",  utils::format_opt(snap.battery.design_capacity_mah, "mAh"), width);
            kv(out, "Capacity full",    utils::format_opt(snap.battery.full_capacity_mah, "mAh"), width);
            kv(out, "Capacity now",     utils::format_opt(snap.battery.current_capacity_mah, "mAh"), width);
            kv(out, "Technology",       snap.battery.technology, width);
            kv(out, "Vendor",           snap.battery.vendor, width);
        }
    }
}

// ---------------------------------------------------------------------------
// Processes view
// ---------------------------------------------------------------------------

void TUI::render_processes_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg,
                                ViewState& view, int width, int height) {
    const std::vector<ProcessStats>& procs = snap.processes;

    {
        std::ostringstream title;
        title << "Processes — " << snap.load.total_processes << " total, "
              << snap.load.running_processes << " running, "
              << snap.load.total_threads << " threads, sorted by "
              << Config::sort_name(cfg.proc_sort);
        out << section_header(title.str(), width) << "\033[K\n";
    }

    // The command name is the one column a row is useless without, so it is
    // the flexible one; the rest are given up in order of usefulness as the
    // terminal narrows.  Only the detailed levels ask for the I/O column at
    // all, which is why it is offered last.
    const bool want_io = cfg.detail_level >= DetailLevel::Detailed;
    int columns = 0;
    const int name_w = std::min(36, flex_column(
        width, 2 + 7 + 2 + 3 /* trailing "  S" */,
        want_io ? std::initializer_list<int>{7, 11, 8, 6, 12, 10, 22}
                : std::initializer_list<int>{7, 11, 8, 6, 12, 10},
        8, &columns));

    const bool show_cpu  = columns >= 1;
    const bool show_rss  = columns >= 2;
    const bool show_mem  = columns >= 3;
    const bool show_thr  = columns >= 4;
    const bool show_user = columns >= 5;
    const bool show_time = columns >= 6;
    const bool show_io   = want_io && columns >= 7;
    const int  user_w    = show_user ? 12 : 0;
    const int  time_w    = show_time ? 10 : 0;
    const int  io_w      = show_io ? 22 : 0;

    out << c_dim() << "  " << utils::fit_right("PID", 7) << "  "
        << utils::fit("COMMAND", static_cast<size_t>(name_w))
        << utils::fit("USER", static_cast<size_t>(user_w))
        << utils::fit_right("CPU%", show_cpu ? 7 : 0)
        << utils::fit_right("MEM%", show_mem ? 8 : 0)
        << utils::fit_right("RSS", show_rss ? 11 : 0)
        << utils::fit_right("THR", show_thr ? 6 : 0)
        << utils::fit_right("TIME", static_cast<size_t>(time_w))
        << (show_io ? utils::fit_right("DISK r/w", static_cast<size_t>(io_w)) : "")
        << "  S" << reset_color() << "\033[K\n";

    // Below the list: the status line, the key hint, and the footer.
    const int viewport   = rows_left(out, height, 4);
    const ListWindow win = clamp_window(view, static_cast<int>(procs.size()), viewport);

    // Publish the selection so [Enter] inspects the row the user is looking at.
    view.selected_pid = (win.cursor >= 0 && win.cursor < static_cast<int>(procs.size()))
                      ? procs[static_cast<size_t>(win.cursor)].pid : -1;

    for (int i = win.first; i < win.first + win.count; ++i) {
        const ProcessStats& p = procs[static_cast<size_t>(i)];
        const bool selected = win.is_cursor(i);

        // The cursor row is inverted rather than coloured, so it stays visible
        // whatever the CPU and memory colours happen to be.
        if (selected) out << "\033[7m";

        out << "  " << (selected ? "" : c_dim()) << utils::fit_right(std::to_string(p.pid), 7)
            << (selected ? "" : reset_color())
            << "  " << (selected ? "" : c_value())
            << utils::column(p.name, static_cast<size_t>(name_w))
            << (selected ? "" : reset_color());
        if (show_user) {
            out << (selected ? "" : c_dim()) << utils::column(p.user, static_cast<size_t>(user_w))
                << (selected ? "" : reset_color());
        }
        if (show_cpu) {
            out << (selected ? "" : usage_color(p.cpu_percent))
                << utils::fit_right(utils::format_opt(std::optional<double>(p.cpu_percent), "", 1), 7)
                << (selected ? "" : reset_color());
        }
        if (show_mem) {
            out << (selected ? "" : usage_color(p.mem_percent))
                << utils::fit_right(utils::format_opt(std::optional<double>(p.mem_percent), "", 1), 8)
                << (selected ? "" : reset_color());
        }
        if (show_rss) {
            out << (selected ? "" : c_value()) << utils::fit_right(format_bytes(p.mem_rss_bytes), 11)
                << (selected ? "" : reset_color());
        }
        if (show_thr) {
            out << (selected ? "" : c_dim()) << utils::fit_right(std::to_string(p.threads), 6)
                << (selected ? "" : reset_color());
        }
        if (show_time) {
            out << (selected ? "" : c_dim())
                << utils::fit_right(utils::format_duration_seconds(p.cpu_time_seconds),
                                    static_cast<size_t>(time_w))
                << (selected ? "" : reset_color());
        }
        if (show_io) {
            const std::string io = utils::format_opt_rate(p.io_read_bytes_per_sec) + " " +
                                   utils::format_opt_rate(p.io_write_bytes_per_sec);
            out << (selected ? "" : c_dim()) << utils::fit_right(io, static_cast<size_t>(io_w))
                << (selected ? "" : reset_color());
        }
        out << "  " << (selected ? "" : c_accent()) << utils::fit(p.state, 1)
            << reset_color() << "\033[K\n";
    }

    render_list_status(out, win, "processes", view, width);
    out << " " << c_dim()
        << utils::column("[Up/Down] select  [PgUp/PgDn] page  [Enter] inspect  "
                         "[o] sort  [a] no limit",
                         static_cast<size_t>(std::max(1, width - 1)))
        << reset_color() << "\033[K\n";
}

// ---------------------------------------------------------------------------
// Single-process inspector
// ---------------------------------------------------------------------------

void TUI::render_process_detail_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg,
                                     ViewState& view, int width, int height) {
    const ProcessStats* proc = nullptr;
    for (const auto& p : snap.processes) {
        if (p.pid == view.selected_pid) { proc = &p; break; }
    }

    if (proc == nullptr) {
        out << section_header("Process", width) << "\033[K\n";
        out << "  " << c_dim()
            << utils::column("Process " + std::to_string(view.selected_pid) +
                             " is gone. [Esc] or [7] returns to the list.",
                             static_cast<size_t>(std::max(1, width - 3)))
            << reset_color() << "\033[K\n";
        return;
    }

    out << section_header("Process " + std::to_string(proc->pid) + " — " + proc->name, width)
        << "\033[K\n";

    kv(out, "Command",        proc->cmdline.empty() ? proc->name : proc->cmdline, width);
    kv(out, "User",           proc->user, width);
    kv(out, "Parent PID",     std::to_string(proc->ppid), width);
    kv(out, "State",          proc->state, width);
    kv(out, "Nice",           utils::format_opt(proc->nice), width);
    kv(out, "Threads",        std::to_string(proc->threads), width);
    if (proc->start_time > 0) kv(out, "Started", utils::format_time(proc->start_time), width);

    out << "\033[K\n" << section_header("Resources", width) << "\033[K\n";
    bar_row(out, "CPU",    proc->cpu_percent, width);
    bar_row(out, "Memory", proc->mem_percent, width);

    kv(out, "CPU time",       utils::format_duration_seconds(proc->cpu_time_seconds), width);
    kv(out, "Resident (RSS)", format_bytes(proc->mem_rss_bytes), width);
    kv(out, "Virtual",        format_bytes(proc->mem_vms_bytes), width);
    kv(out, "Open files",     utils::format_opt(proc->open_files), width);
    kv(out, "Disk read",      utils::format_opt_rate(proc->io_read_bytes_per_sec), width);
    kv(out, "Disk write",     utils::format_opt_rate(proc->io_write_bytes_per_sec), width);
    kv(out, "Disk total r/w",
       utils::format_opt_bytes(proc->io_read_bytes_total) + " / " +
       utils::format_opt_bytes(proc->io_write_bytes_total), width);
    kv(out, "Network sent",   utils::format_opt_rate(proc->tx_bytes_per_sec), width);
    kv(out, "Network received",
       proc->rx_bytes_per_sec.has_value()
         ? utils::format_opt_rate(proc->rx_bytes_per_sec)
         : std::string("N/A (no per-process receive counter)"), width);
    kv(out, "Sockets",        utils::format_opt(proc->socket_count), width);

    // This process's own connections, picked out of the global list.
    std::vector<const NetConnectionStats*> own;
    for (const auto& c : snap.connections) {
        if (c.pid == proc->pid) own.push_back(&c);
    }
    if (!own.empty()) {
        out << "\033[K\n" << section_header("Connections", width) << "\033[K\n";
        const int addr_w = std::max(14, std::min(28, (width - 30) / 2));
        // Half of what is free; the open-files section takes the rest.
        const int rows   = std::max(2, rows_left(out, height, 8) / 2);
        for (int i = 0; i < rows && i < static_cast<int>(own.size()); ++i) {
            const NetConnectionStats& c = *own[static_cast<size_t>(i)];
            out << "  " << c_dim() << utils::column(c.protocol, 7) << reset_color()
                << c_value() << utils::column(c.local_addr + ":" + std::to_string(c.local_port),
                                              static_cast<size_t>(addr_w)) << reset_color()
                << c_value() << utils::column(c.remote_port == 0 ? "-"
                                              : c.remote_addr + ":" + std::to_string(c.remote_port),
                                              static_cast<size_t>(addr_w)) << reset_color()
                << c_dim() << c.state << reset_color() << "\033[K\n";
        }
        if (static_cast<int>(own.size()) > rows) {
            out << "  " << c_dim() << "… " << (own.size() - static_cast<size_t>(rows))
                << " more" << reset_color() << "\033[K\n";
        }
    }

    // Open descriptors: what the process is actually reading and writing.
    out << "\033[K\n" << section_header("Open files", width) << "\033[K\n";
    if (!snap.selected_process_files.has_value()) {
        out << "  " << c_dim() << "Not collected." << reset_color() << "\033[K\n";
        return;
    }

    const OpenFilesResult& files = *snap.selected_process_files;
    if (files.status != OpenFilesStatus::Ok) {
        out << "  " << c_dim()
            << utils::column(files.reason(), static_cast<size_t>(std::max(1, width - 3)))
            << reset_color() << "\033[K\n";
        return;
    }
    if (files.files.empty()) {
        out << "  " << c_dim() << "None open." << reset_color() << "\033[K\n";
        return;
    }

    const int path_w = std::max(20, width - 26);
    out << c_dim() << "  " << utils::fit_right("FD", 5) << "  "
        << utils::fit("TYPE", 8) << utils::fit("MODE", 5)
        << utils::fit("PATH", static_cast<size_t>(path_w))
        << reset_color() << "\033[K\n";

    const int rows = rows_left(out, height, 2);
    int shown = 0;
    for (const auto& f : files.files) {
        if (++shown > rows && cfg.detail_level < DetailLevel::Full) break;
        out << "  " << c_dim() << utils::fit_right(std::to_string(f.fd), 5) << reset_color()
            << "  " << c_dim()   << utils::column(f.type, 8) << reset_color()
            << c_dim()   << utils::column(f.mode, 5) << reset_color()
            << c_value() << utils::column(f.path, static_cast<size_t>(path_w)) << reset_color()
            << "\033[K\n";
    }
    if (shown > rows) {
        out << "  " << c_dim() << "… " << (files.files.size() - static_cast<size_t>(rows))
            << " more; press [+] for full detail" << reset_color() << "\033[K\n";
    }
}
