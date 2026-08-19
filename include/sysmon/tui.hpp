/**
 * @file tui.hpp
 * @brief ANSI-based live TUI dashboard for sysmon with full customization.
 */

#ifndef SYSMON_TUI_HPP
#define SYSMON_TUI_HPP

#include "sysmon/stats.hpp"
#include "sysmon/config.hpp"
#include <string>
#include <vector>
#include <deque>
#include <cstdint>

/**
 * @brief Full-screen live dashboard renderer with dynamic customization.
 */
class TUI {
public:
    static constexpr int HISTORY_LEN = 60; ///< Sparkline history length

    TUI();
    ~TUI();

    /**
     * @brief Perform a full screen render with all data and active configuration.
     */
    void render(const SystemStats&                   system,
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
                const Config&                        cfg);

    /** @brief Clear the screen and reset cursor. */
    void clear();

    /** @brief Hide the terminal cursor. */
    void hide_cursor();

    /** @brief Show the terminal cursor again. */
    void show_cursor();

    /** @brief Get terminal width. */
    int terminal_width() const;

    /** @brief Get terminal height. */
    int terminal_height() const;

private:
    // History buffers for sparklines
    std::deque<double> cpu_history_;
    std::deque<double> mem_history_;
    std::deque<double> gpu_history_;
    std::deque<double> net_rx_history_;
    std::deque<double> net_tx_history_;
    std::deque<double> cpu_temp_history_;

    // ANSI helpers
    std::string move_to(int row, int col) const;
    std::string color_fg(int r, int g, int b) const;
    std::string color_bg(int r, int g, int b) const;
    std::string reset_color() const;
    std::string bold() const;
    std::string dim() const;

    // Color theme
    std::string c_title()    const;
    std::string c_label()    const;
    std::string c_value()    const;
    std::string c_good()     const;
    std::string c_warn()     const;
    std::string c_danger()   const;
    std::string c_accent()   const;
    std::string c_border()   const;
    std::string c_dim()      const;

    // Section renderers
    void render_header(std::ostringstream& out, const SystemStats& sys, int width, bool compact);
    void render_cpu_section(std::ostringstream& out, const CpuStats& cpu, int width, const Config& cfg);
    void render_gpu_section(std::ostringstream& out, const std::vector<GpuStats>& gpus, int width, const Config& cfg);
    void render_memory_section(std::ostringstream& out, const MemoryStats& mem, int width, const Config& cfg);
    void render_load_section(std::ostringstream& out, const LoadStats& load, int width);
    void render_network_section(std::ostringstream& out, const std::vector<NetworkStats>& net, int width, const Config& cfg);
    void render_connections_section(std::ostringstream& out, const std::vector<NetConnectionStats>& conns, int width, const Config& cfg);
    void render_disk_section(std::ostringstream& out, const std::vector<DiskStats>& disks, const std::vector<DiskIOStats>& io, int width, const Config& cfg);
    void render_temperature_section(std::ostringstream& out, const TemperatureStats& temps, int width, const Config& cfg);
    void render_process_section(std::ostringstream& out, const std::vector<ProcessStats>& procs, int width, const Config& cfg);
    void render_footer(std::ostringstream& out, int width, const Config& cfg);

    // Widget primitives
    std::string progress_bar(double percent, int width, bool colored = true) const;
    std::string sparkline(const std::deque<double>& history, int width) const;
    std::string section_header(const std::string& title, int width) const;
    std::string horizontal_rule(int width) const;

    // Utilities
    void push_history(std::deque<double>& hist, double value);
    std::string format_bytes(uint64_t bytes) const;
    std::string format_bytes_per_sec(double bps) const;
    std::string usage_color(double percent) const;
    std::string temp_color(double celsius) const;

    mutable int term_width_{80};
    mutable int term_height_{24};
    void update_terminal_size() const;
};

#endif // SYSMON_TUI_HPP
