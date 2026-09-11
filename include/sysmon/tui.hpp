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
#include <initializer_list>
#include <sstream>
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
     * @brief Perform a full screen render of one snapshot.
     *
     * @param view Mutable: the renderer is the only place that knows how many
     *             rows the viewport and the data have, so it is what clamps a
     *             scroll offset and resolves a selection back to a real row.
     */
    void render(const Snapshot& snap, const Config& cfg, ViewState& view);

    /** @brief Render the overview, for callers with no interactive state. */
    void render(const Snapshot& snap, const Config& cfg);

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

    /**
     * @brief The slice of a long list that is currently on screen.
     *
     * Produced by clamp_window(), which is also what writes the corrected
     * offset back into the ViewState — so a scroll past the end settles at the
     * end instead of showing blank space forever.
     */
    struct ListWindow {
        int first{0};    ///< Index of the first visible row
        int count{0};    ///< Number of visible rows
        int total{0};    ///< Rows the list has in all
        int cursor{-1};  ///< Row the cursor is on, or -1 when the list is empty
        bool has_more_above() const { return first > 0; }
        bool has_more_below() const { return first + count < total; }
        bool is_cursor(int row) const { return row == cursor; }
    };

    /**
     * @brief Resolve the visible slice of a list and settle the cursor in it.
     *
     * Writes the clamped cursor and the derived scroll offset back into @p view,
     * which is what stops a held-down arrow key from parking the list past its
     * end and keeps the cursor on screen while it moves.
     */
    ListWindow clamp_window(ViewState& view, int total_rows, int viewport_rows) const;

    /**
     * @brief How many rows are still free below what has been written.
     *
     * Counts the newlines already in the frame instead of subtracting a
     * hand-counted constant, so a view that gains or loses a line does not
     * silently start clipping its list — the constants drifted out of date the
     * moment any section above them changed.
     *
     * @param reserve Rows to keep for whatever is printed after the list.
     */
    int rows_left(const std::ostringstream& out, int height, int reserve) const;

    /**
     * @brief Cut an assembled frame to at most @p rows painted lines.
     *
     * The last line of defence for the vertical axis.  Lists adapt through
     * rows_left(), but the fixed content above them does not: on a 15-row
     * terminal the memory view still has a dozen labelled values to print, and
     * a frame taller than the terminal makes it *scroll* — which moves every
     * later frame's cursor-home to the wrong place and tears the display apart.
     * Dropping the overflow is strictly better than scrolling.
     */
    static std::string clip_frame(const std::string& frame, int rows);

    /**
     * @brief Width for a table's flexible column, dropping optional ones to fit.
     *
     * Table layouts kept being written as a fixed cost plus a flexible column
     * with its own minimum, and the two could add up past the terminal —
     * five separate tables overflowed below 55 columns that way.  Here the
     * optional columns are given up one at a time, least valuable last, until
     * the flexible column has room; the row can then never exceed @p width.
     *
     * @param width     Terminal width.
     * @param fixed     Always-present columns, margins and gaps included.
     * @param optional  Optional column widths, most valuable first.
     * @param min_flex  Smallest useful width for the flexible column.
     * @param taken     Out: how many leading optional columns fit.
     * @return Width for the flexible column, at least 1.
     */
    static int flex_column(int width, int fixed, std::initializer_list<int> optional,
                           int min_flex, int* taken);

    // Full-screen focus views
    void render_overview(std::ostringstream& out, const Snapshot& snap, const Config& cfg, int width);
    void render_cpu_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg, ViewState& view, int width, int height);
    void render_memory_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg, int width);
    void render_gpu_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg, int width);
    void render_disk_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg, ViewState& view, int width, int height);
    void render_network_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg, ViewState& view, int width, int height);
    void render_connections_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg, ViewState& view, int width, int height);
    void render_processes_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg, ViewState& view, int width, int height);
    void render_sensors_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg, ViewState& view, int width, int height);
    void render_process_detail_view(std::ostringstream& out, const Snapshot& snap, const Config& cfg, ViewState& view, int width, int height);

    /** @brief The strip of view names across the top of a focus view. */
    void render_view_bar(std::ostringstream& out, const ViewState& view, const Config& cfg, int width);

    /** @brief "rows 20-40 of 771" plus the scroll hints, under a long list. */
    void render_list_status(std::ostringstream& out, const ListWindow& window,
                            const std::string& noun, const ViewState& view, int width);

    /** @brief A "Label  value" line, skipped entirely when the value is empty. */
    void kv(std::ostringstream& out, const std::string& label, const std::string& value, int width);

    /**
     * @brief A "Label [====    ]  42.0 %" row that always fits @p width.
     *
     * One place for the arithmetic: six views drew this row with their own
     * hand-tuned constants, and each one was its own chance to overflow a
     * narrow terminal.
     */
    void bar_row(std::ostringstream& out, const std::string& label, double percent, int width);

    // Section renderers
    void render_header(std::ostringstream& out, const SystemStats& sys, int width, bool compact);
    void render_cpu_section(std::ostringstream& out, const CpuStats& cpu, int width, const Config& cfg);
    void render_gpu_section(std::ostringstream& out, const std::vector<GpuStats>& gpus, int width, const Config& cfg);
    void render_memory_section(std::ostringstream& out, const MemoryStats& mem, int width, const Config& cfg);
    void render_load_section(std::ostringstream& out, const LoadStats& load, int width);
    void render_battery_section(std::ostringstream& out, const BatteryStats& battery, int width);
    void render_network_section(std::ostringstream& out, const std::vector<NetworkStats>& net,
                                const NetGlobalStats& global, int width, const Config& cfg);
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
