/**
 * @file main.cpp
 * @brief sysmon – A comprehensive, cross-platform terminal system monitor.
 */

#include "sysmon/battery_monitor.hpp"
#include "sysmon/config.hpp"
#include "sysmon/cpu_monitor.hpp"
#include "sysmon/disk_io_monitor.hpp"
#include "sysmon/disk_monitor.hpp"
#include "sysmon/gpu_monitor.hpp"
#include "sysmon/json_renderer.hpp"
#include "sysmon/load_monitor.hpp"
#include "sysmon/memory_monitor.hpp"
#include "sysmon/net_connections_monitor.hpp"
#include "sysmon/network_monitor.hpp"
#include "sysmon/platform.hpp"
#include "sysmon/process_monitor.hpp"
#include "sysmon/system_monitor.hpp"
#include "sysmon/temperature_monitor.hpp"
#include "sysmon/terminal.hpp"
#include "sysmon/utils.hpp"
#include "sysmon/text_renderer.hpp"
#include "sysmon/tui.hpp"
#include "sysmon/version.hpp"

#include <atomic>
#include <chrono>
#include <climits>
#include <csignal>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

static constexpr const char* VERSION = SYSMON_VERSION;

// ---------------------------------------------------------------------------
// Signal handling
// ---------------------------------------------------------------------------

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_force_refresh{false};

static void signal_handler(int) {
    g_running = false;
}

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------

static void print_help() {
    std::cout
        << "sysmon v" << VERSION << " - Comprehensive Cross-Platform System Monitor\n"
        << "Built for " << SYSMON_PLATFORM_NAME << "\n\n"
        << "Usage:\n"
        << "  sysmon [options]\n\n"
        << "Output Modes:\n"
        << "  --once                 Print metrics once and exit\n"
        << "  --no-tui               Plain text output (no ANSI formatting)\n"
        << "  --json                 Emit one JSON snapshot and exit (implies --once)\n"
        << "  --json-compact         As --json, but on a single line\n"
        << "  --compact, -m          Shorthand for --detail compact\n"
        << "  --detail LEVEL         Density: compact, normal, detailed, full\n"
        << "  --view NAME            Start in: overview, cpu, memory, gpu, disk,\n"
        << "                         network, connections, processes, sensors\n"
        << "  --interval N, -i N     Update interval in seconds (default: 2)\n"
        << "  --limit N              Max processes to display (0 = all)\n"
        << "  --all                  Show every process and connection, no limits\n"
        << "  --sort KEY             Process sort: cpu, mem, pid, name, time\n\n"
        << "Component Visibility Toggles:\n"
        << "  --cores / --no-cores   Show / hide individual CPU cores\n"
        << "  --gpu / --no-gpu       Show / hide GPU & VRAM statistics\n"
        << "  --conn / --no-conn     Show / hide active network connections\n"
        << "  --proc / --no-proc     Show / hide top processes table\n"
        << "  --net / --no-net       Show / hide network interfaces\n"
        << "  --temp / --no-temp     Show / hide temperatures & sensors\n"
        << "  --disk / --no-disk     Show / hide storage & disk I/O\n"
        << "  --battery / --no-battery   Show / hide battery & power\n"
        << "  --net-details          Per-interface MAC, MTU, totals and errors\n"
        << "  --all-interfaces       Include interfaces that carry no traffic\n"
        << "  --listen               Include listening sockets in connections\n\n"
        << "Configuration:\n"
        << "  --config PATH          Load configuration from PATH\n"
        << "  --generate-config      Write default config to the config file location\n"
        << "  --show-config          Print currently active configuration and exit\n"
        << "  --version, -v          Show version information\n"
        << "  --help, -h             Show this help\n\n"
        << "Interactive Hotkeys (TUI Mode):\n"
        << "  Views    [0] Overview  [1] CPU     [2] Memory  [3] GPU    [4] Disk\n"
        << "           [5] Network   [6] Conns   [7] Procs   [8] Sensors\n"
        << "           [Tab] Next view          [ESC] Back to overview\n"
        << "  Density  [+] More detail  [-] Less detail  [m] Compact on/off\n"
        << "  Lists    [Up/Down] Move  [PgUp/PgDn] Page  [Home/End] Jump\n"
        << "           [a] Show all (no limit)  [Enter] Inspect selected process\n"
        << "  Sections [c] CPU cores  [g] GPU     [n] Network  [v] Connections\n"
        << "           [p] Processes  [t] Sensors [d] Storage  [b] Battery\n"
        << "  Other    [o] Cycle sort [s] Save config [r] Refresh [q] Quit\n";
}

// ---------------------------------------------------------------------------
// Interactive key handling
// ---------------------------------------------------------------------------

namespace {

/// The views [Tab] cycles through, in order.  View::Process is excluded: it is
/// reached by selecting a process, not by cycling.
constexpr View kCycleViews[] = {
    View::Overview, View::Cpu, View::Memory, View::Gpu, View::Disk,
    View::Network, View::Connections, View::Processes, View::Sensors
};

View next_view(View current) {
    for (std::size_t i = 0; i < std::size(kCycleViews); ++i) {
        if (kCycleViews[i] == current) {
            return kCycleViews[(i + 1) % std::size(kCycleViews)];
        }
    }
    return View::Overview;
}

DetailLevel more_detail(DetailLevel level) {
    switch (level) {
        case DetailLevel::Compact:  return DetailLevel::Normal;
        case DetailLevel::Normal:   return DetailLevel::Detailed;
        case DetailLevel::Detailed: return DetailLevel::Full;
        case DetailLevel::Full:     return DetailLevel::Full;
    }
    return level;
}

DetailLevel less_detail(DetailLevel level) {
    switch (level) {
        case DetailLevel::Full:     return DetailLevel::Detailed;
        case DetailLevel::Detailed: return DetailLevel::Normal;
        case DetailLevel::Normal:   return DetailLevel::Compact;
        case DetailLevel::Compact:  return DetailLevel::Compact;
    }
    return level;
}

/// Switch views, resetting the scroll position.
///
/// Carrying a scroll offset from a 700-row process list into a 4-row disk list
/// would show an empty screen, so every view change starts at the top.
void go_to(ViewState& view, View target) {
    if (view.view != target) {
        view.view          = target;
        view.cursor        = 0;
        view.scroll_offset = 0;
    }
}

void handle_key(const terminal::KeyEvent& ev,
                Config& cfg,
                ViewState& view,
                std::atomic<bool>& running,
                std::atomic<bool>& force_refresh) {
    using terminal::Key;

    bool handled = true;
    switch (ev.key) {
        // ---- Navigation --------------------------------------------------
        case Key::Escape:
            // "Back": out of a focus view first, out of the program only from
            // the overview, so a mis-typed view change is not a quit.
            if (view.view == View::Overview) running = false;
            else                             go_to(view, View::Overview);
            break;

        case Key::Tab:   go_to(view, next_view(view.view)); break;

        // The cursor is moved freely here and clamped by the renderer, which is
        // the only place that knows how many rows the list actually has.
        case Key::Up:       --view.cursor;            break;
        case Key::Down:     ++view.cursor;            break;
        case Key::PageUp:   view.cursor -= 10;        break;
        case Key::PageDown: view.cursor += 10;        break;
        case Key::Home:     view.cursor = 0;          break;
        case Key::End:      view.cursor = INT_MAX / 2; break;

        case Key::Enter:
            // Inspect whatever the process list has selected.
            if (view.selected_pid > 0) go_to(view, View::Process);
            else                       go_to(view, View::Processes);
            break;

        case Key::F1: go_to(view, View::Cpu);         break;
        case Key::F2: go_to(view, View::Memory);      break;
        case Key::F3: go_to(view, View::Gpu);         break;
        case Key::F4: go_to(view, View::Disk);        break;
        case Key::F5: force_refresh = true;           break;

        case Key::Char: handled = false; break;
        default:        handled = false; break;
    }

    if (handled) {
        if (view.cursor < 0) view.cursor = 0;
        force_refresh = true;
        return;
    }

    switch (ev.ch) {
        case 'q': case 'Q': case 3: running = false; return;

        // ---- Views -------------------------------------------------------
        case '0': go_to(view, View::Overview);    break;
        case '1': go_to(view, View::Cpu);         break;
        case '2': go_to(view, View::Memory);      break;
        case '3': go_to(view, View::Gpu);         break;
        case '4': go_to(view, View::Disk);        break;
        case '5': go_to(view, View::Network);     break;
        case '6': go_to(view, View::Connections); break;
        case '7': go_to(view, View::Processes);   break;
        case '8': go_to(view, View::Sensors);     break;

        // ---- Density -----------------------------------------------------
        case '+': case '=': cfg.detail_level = more_detail(cfg.detail_level); break;
        case '-': case '_': cfg.detail_level = less_detail(cfg.detail_level); break;
        case 'm': case 'M':
            cfg.detail_level = cfg.compact_mode() ? DetailLevel::Normal
                                                  : DetailLevel::Compact;
            break;

        // ---- Lists -------------------------------------------------------
        case 'a': case 'A':
            view.show_all      = !view.show_all;
            view.cursor        = 0;
            view.scroll_offset = 0;
            break;
        case 'k': --view.cursor; break;   // vi-style, alongside the arrows
        case 'j': ++view.cursor; break;

        // ---- Section toggles (unchanged) ---------------------------------
        case 'c': case 'C': cfg.show_cpu_per_core = !cfg.show_cpu_per_core; break;
        case 'g': case 'G': cfg.show_gpu          = !cfg.show_gpu;          break;
        case 'n': case 'N': cfg.show_network      = !cfg.show_network;      break;
        case 'v': case 'V': cfg.show_connections  = !cfg.show_connections;  break;
        case 'p': case 'P': cfg.show_processes    = !cfg.show_processes;    break;
        case 't': case 'T': cfg.show_temperature  = !cfg.show_temperature;  break;
        case 'd': case 'D': cfg.show_disk         = !cfg.show_disk;         break;
        case 'b': case 'B': cfg.show_battery      = !cfg.show_battery;      break;

        case 'r': case 'R': break;   // force_refresh below is the whole action
        case 's': case 'S': cfg.save(); break;

        case 'o': case 'O':
            switch (cfg.proc_sort) {
                case ProcSort::Cpu:    cfg.proc_sort = ProcSort::Memory; break;
                case ProcSort::Memory: cfg.proc_sort = ProcSort::Time;   break;
                case ProcSort::Time:   cfg.proc_sort = ProcSort::Pid;    break;
                case ProcSort::Pid:    cfg.proc_sort = ProcSort::Name;   break;
                case ProcSort::Name:   cfg.proc_sort = ProcSort::Cpu;    break;
            }
            break;

        default: return;   // Unbound key: do not even repaint.
    }

    if (view.cursor < 0) view.cursor = 0;
    force_refresh = true;
}

} // namespace

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    // ---- Pass 1: locate --config first ---------------------------------
    // Loading a config file replaces the whole Config object, so it has to
    // happen before any flag is applied; otherwise "sysmon --no-gpu --config f"
    // would silently discard --no-gpu.
    std::string config_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    Config cfg = config_path.empty() ? Config::load() : Config::load_from(config_path);

    bool once_flag_set   = false;
    bool no_tui_flag_set = false;
    bool json_flag_set   = false;
    bool json_compact    = false;
    bool limit_flag_set  = false;
    bool show_all_flag_set = false;

    // ---- Pass 2: apply the remaining flags ------------------------------
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") { print_help(); return 0; }
        if (arg == "--version" || arg == "-v") {
            std::cout << "sysmon " << VERSION << " (" << SYSMON_PLATFORM_NAME << ")\n";
            return 0;
        }
        if (arg == "--generate-config") {
            Config def = Config::defaults();
            def.save();
            std::cout << "Generated default config at: " << Config::default_config_path() << "\n";
            return 0;
        }
        if (arg == "--show-config") { std::cout << cfg.to_string(); return 0; }

        if (arg == "--config") { ++i; continue; }   // already consumed in pass 1

        if (arg == "--once")         { once_flag_set = true;   continue; }
        if (arg == "--no-tui")       { no_tui_flag_set = true; continue; }
        if (arg == "--json")         { json_flag_set = true;   continue; }
        if (arg == "--json-compact") { json_flag_set = true; json_compact = true; continue; }
        if (arg == "--compact" || arg == "-m") {
            cfg.detail_level = DetailLevel::Compact;
            continue;
        }
        if (arg == "--detail") {
            if (i + 1 >= argc) {
                std::cerr << "sysmon: --detail requires a value\n";
                return 2;
            }
            const auto level = Config::parse_detail(argv[++i]);
            if (!level.has_value()) {
                std::cerr << "sysmon: unknown detail level '" << argv[i]
                          << "' (expected compact, normal, detailed or full)\n";
                return 2;
            }
            cfg.detail_level = level.value();
            continue;
        }
        if (arg == "--view") {
            if (i + 1 >= argc) {
                std::cerr << "sysmon: --view requires a value\n";
                return 2;
            }
            const auto view = Config::parse_view(argv[++i]);
            if (!view.has_value()) {
                std::cerr << "sysmon: unknown view '" << argv[i]
                          << "' (expected overview, cpu, memory, gpu, disk, "
                             "network, connections, processes or sensors)\n";
                return 2;
            }
            cfg.start_view = view.value();
            continue;
        }
        if (arg == "--all") {
            // "Show me everything" — no list is truncated in any renderer.
            cfg.proc_limit        = 0;
            cfg.connections_limit = 0;
            cfg.connections_show_listen = true;
            show_all_flag_set     = true;
            continue;
        }

        if (arg == "--interval" || arg == "-i") {
            if (i + 1 >= argc) {
                std::cerr << "sysmon: " << arg << " requires a value\n";
                return 2;
            }
            const auto value = utils::to_int(argv[++i]);
            if (!value.has_value() || *value < 1) {
                std::cerr << "sysmon: invalid interval '" << argv[i] << "' (expected >= 1)\n";
                return 2;
            }
            cfg.refresh_interval = static_cast<int>(*value);
            continue;
        }
        if (arg == "--limit") {
            if (i + 1 >= argc) {
                std::cerr << "sysmon: --limit requires a value\n";
                return 2;
            }
            const auto value = utils::to_int(argv[++i]);
            if (!value.has_value() || *value < 0) {
                std::cerr << "sysmon: invalid limit '" << argv[i] << "'\n";
                return 2;
            }
            cfg.proc_limit = static_cast<int>(*value);
            limit_flag_set = true;
            continue;
        }
        if (arg == "--sort") {
            if (i + 1 >= argc) {
                std::cerr << "sysmon: --sort requires a value\n";
                return 2;
            }
            const auto sort = Config::parse_sort(argv[++i]);
            if (!sort.has_value()) {
                std::cerr << "sysmon: unknown sort key '" << argv[i]
                          << "' (expected cpu, mem, pid, name or time)\n";
                return 2;
            }
            cfg.proc_sort = sort.value();
            continue;
        }

        if (arg == "--cores")      { cfg.show_cpu_per_core = true;  continue; }
        if (arg == "--no-cores")   { cfg.show_cpu_per_core = false; continue; }
        if (arg == "--gpu")        { cfg.show_gpu = true;           continue; }
        if (arg == "--no-gpu")     { cfg.show_gpu = false;          continue; }
        if (arg == "--conn")       { cfg.show_connections = true;   continue; }
        if (arg == "--no-conn")    { cfg.show_connections = false;  continue; }
        if (arg == "--proc")       { cfg.show_processes = true;     continue; }
        if (arg == "--no-proc")    { cfg.show_processes = false;    continue; }
        if (arg == "--net")        { cfg.show_network = true;       continue; }
        if (arg == "--no-net")     { cfg.show_network = false;      continue; }
        if (arg == "--temp")       { cfg.show_temperature = true;   continue; }
        if (arg == "--no-temp")    { cfg.show_temperature = false;  continue; }
        if (arg == "--disk")       { cfg.show_disk = true;          continue; }
        if (arg == "--no-disk")    { cfg.show_disk = false;         continue; }
        if (arg == "--battery")    { cfg.show_battery = true;       continue; }
        if (arg == "--no-battery") { cfg.show_battery = false;      continue; }
        if (arg == "--net-details")    { cfg.show_network_details = true;  continue; }
        if (arg == "--all-interfaces") { cfg.show_network_inactive = true; continue; }
        if (arg == "--listen")         { cfg.connections_show_listen = true; continue; }

        // An unrecognised flag used to be ignored in silence, so a typo like
        // "--limt 5" looked as if it had worked.
        std::cerr << "sysmon: unknown option '" << arg << "'\n"
                  << "Try 'sysmon --help' for the list of options.\n";
        return 2;
    }

    terminal::init();

    const bool stdout_is_tty = terminal::stdout_is_tty();
    const bool stdin_is_tty  = terminal::stdin_is_tty();

    const bool is_once   = json_flag_set || once_flag_set || !cfg.tui_enabled ||
                           (!stdout_is_tty && !no_tui_flag_set);
    const bool is_no_tui = no_tui_flag_set || !stdout_is_tty;

    // ---- Monitors --------------------------------------------------------
    SystemMonitor          sys_mon;
    CpuMonitor             cpu_mon;
    MemoryMonitor          mem_mon;
    GpuMonitor             gpu_mon;
    LoadMonitor            load_mon;
    BatteryMonitor         battery_mon;
    DiskMonitor            disk_mon;
    DiskIOMonitor          disk_io_mon;
    TemperatureMonitor     temp_mon;
    NetworkMonitor         net_mon;
    NetConnectionsMonitor  conn_mon;
    ProcessMonitor         proc_mon;

    // Ephemeral dashboard state.  Deliberately not part of Config: [s] saves
    // settings, and a scroll offset is not a setting.
    ViewState view;
    view.view      = cfg.start_view;
    view.show_all  = show_all_flag_set;

    std::unique_ptr<TUI> tui;
    TextRenderer text_renderer;
    JsonRenderer json_renderer;
    json_renderer.set_pretty(!json_compact);

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    bool raw_mode = false;
    if (!is_once && !is_no_tui && stdin_is_tty) {
        raw_mode = terminal::enable_raw_input();
        tui = std::make_unique<TUI>();
    }

    auto collect = [&]() {
        // --json is a data export, not a view.  If the display toggles shaped
        // it, "processes": [] would mean "none exist" on one machine and "the
        // config file hid them" on another, and a stale show_gpu=false in
        // ~/.config would silently truncate every snapshot.  So JSON always
        // collects everything.
        Config collect_cfg = cfg;
        if (json_flag_set) {
            collect_cfg.show_cpu         = true;
            collect_cfg.show_memory      = true;
            collect_cfg.show_gpu         = true;
            collect_cfg.show_battery     = true;
            collect_cfg.show_disk        = true;
            collect_cfg.show_disk_io     = true;
            collect_cfg.show_temperature = true;
            collect_cfg.show_network     = true;
            collect_cfg.show_connections = true;
            collect_cfg.show_processes   = true;
            collect_cfg.detail_level     = DetailLevel::Full;
            collect_cfg.connections_show_listen = true;
            // The list limits exist to fit a terminal, so they must not shape a
            // data export either: a stale "proc_limit = 5" in ~/.config would
            // otherwise truncate every snapshot, and a truncated array is
            // indistinguishable from a complete one.  0 means "everything".
            // An explicit --limit is a deliberate request and is still honoured;
            // connections have no CLI override at all, so they are always full.
            if (!limit_flag_set) collect_cfg.proc_limit = 0;
            collect_cfg.connections_limit = 0;
        } else {
            // A focus view is an explicit request for that subsystem, so it
            // overrides both the section toggle and the compact-mode skip:
            // pressing [7] must show the process list even when [p] hid it,
            // otherwise the key looks broken.
            switch (view.view) {
                case View::Cpu:         collect_cfg.show_cpu = true;         break;
                case View::Memory:      collect_cfg.show_memory = true;      break;
                case View::Gpu:         collect_cfg.show_gpu = true;         break;
                case View::Disk:        collect_cfg.show_disk = true;
                                        collect_cfg.show_disk_io = true;     break;
                case View::Network:
                    collect_cfg.show_network = true;
                    // Upload-by-process is derived from the socket table, so
                    // the network view needs it collected even when [v] hid
                    // the connections section.
                    collect_cfg.show_connections = true;
                    if (collect_cfg.compact_mode()) collect_cfg.detail_level = DetailLevel::Normal;
                    break;
                case View::Sensors:     collect_cfg.show_temperature = true; break;
                // A focus view collapsed to a single line would be empty, so
                // the compact level does not apply inside one.
                case View::Connections:
                    collect_cfg.show_connections = true;
                    if (collect_cfg.compact_mode()) collect_cfg.detail_level = DetailLevel::Normal;
                    break;
                case View::Processes:
                case View::Process:
                    collect_cfg.show_processes = true;
                    if (collect_cfg.compact_mode()) collect_cfg.detail_level = DetailLevel::Normal;
                    break;
                case View::Overview:
                    break;
            }

            // [a] means "no limits" — the point of the key is to see the rows
            // the dashboard's own limits hid.
            if (view.show_all) {
                collect_cfg.proc_limit        = 0;
                collect_cfg.connections_limit = 0;
                collect_cfg.connections_show_listen = true;
            } else if ((view.view == View::Processes || view.view == View::Process) &&
                       !limit_flag_set) {
                // A dedicated, scrollable list is not limited by a number
                // chosen to fit alongside every other section.  An explicit
                // --limit is still a deliberate request and is left alone.
                collect_cfg.proc_limit = 0;
            } else if (view.view == View::Connections) {
                collect_cfg.connections_limit = 0;
            }
        }

        Snapshot snap;
        snap.system  = sys_mon.read();
        snap.cpu     = cpu_mon.read();
        snap.memory  = mem_mon.read();
        snap.load    = load_mon.read();
        snap.gpus    = collect_cfg.show_gpu ? gpu_mon.read() : std::vector<GpuStats>{};
        snap.battery = collect_cfg.show_battery ? battery_mon.read() : BatteryStats{};
        snap.disks   = collect_cfg.show_disk ? disk_mon.read() : std::vector<DiskStats>{};
        snap.disk_io = (collect_cfg.show_disk && collect_cfg.show_disk_io)
                     ? disk_io_mon.read() : std::vector<DiskIOStats>{};
        snap.temperatures = collect_cfg.show_temperature ? temp_mon.read() : TemperatureStats{};
        snap.network = collect_cfg.show_network ? net_mon.read() : std::vector<NetworkStats>{};
        // Always read the complete socket table and truncate afterwards.  The
        // monitor parses everything either way, and bandwidth attribution
        // needs the whole of it — crediting a process from a list already cut
        // to what fits a terminal would report a fraction of its traffic.
        snap.connections = (collect_cfg.show_connections && !collect_cfg.compact_mode())
                         ? conn_mon.read(collect_cfg.connections_show_listen, 0)
                         : std::vector<NetConnectionStats>{};
        snap.processes = (collect_cfg.show_processes && !collect_cfg.compact_mode())
                       ? proc_mon.read(static_cast<unsigned int>(collect_cfg.proc_limit),
                                       collect_cfg.proc_sort)
                       : std::vector<ProcessStats>{};

        // Descriptor tables are read for the one inspected process only; doing
        // it for the whole table would cost thousands of syscalls per refresh.
        if (!json_flag_set && view.view == View::Process && view.selected_pid > 0) {
            snap.selected_process_files = ProcessMonitor::open_files(view.selected_pid);
        }

        if (collect_cfg.show_network) {
            snap.net_global = net_mon.read_global();
            NetConnectionsMonitor::summarize(snap.connections, snap.net_global);
        }

        if (!snap.processes.empty()) {
            conn_mon.attribute_bandwidth(snap.connections, snap.processes);
        }

        // Only now cut the connection list down to what the view asked for.
        if (collect_cfg.connections_limit > 0 &&
            snap.connections.size() > static_cast<size_t>(collect_cfg.connections_limit)) {
            snap.connections.resize(static_cast<size_t>(collect_cfg.connections_limit));
        }

        // Load normalised per core is what actually says whether the machine is
        // saturated, so derive it once here rather than in each renderer.
        if (snap.cpu.logical_cores > 0) {
            snap.load.load_per_core_1min =
                snap.load.load_1min / static_cast<double>(snap.cpu.logical_cores);
        }
        return snap;
    };

    auto collect_and_render = [&]() {
        try {
            const Snapshot snap = collect();
            if (json_flag_set) {
                json_renderer.render(snap, cfg);
            } else if (is_once || is_no_tui || !tui) {
                text_renderer.render(snap, cfg);
            } else {
                tui->render(snap, cfg, view);
            }
        } catch (const std::exception& e) {
            terminal::disable_raw_input();
            std::cerr << "\nError: " << e.what() << "\n";
            g_running = false;
        }
    };

    // Warm-up sample: every rate in sysmon is a delta between two readings, so
    // without this first sample the initial frame would show all zeroes.
    try {
        cpu_mon.read();
        mem_mon.read();
        net_mon.read();
        disk_io_mon.read();
        proc_mon.read(1);

        // Per-process bandwidth is a difference between two socket-table
        // samples, so without a baseline here every rate in a --once or
        // --json run would be N/A.
        std::vector<ProcessStats> warmup_procs;
        conn_mon.attribute_bandwidth(conn_mon.read(true, 0), warmup_procs);
    } catch (...) {}
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    if (is_once) {
        collect_and_render();
        terminal::shutdown();
        return 0;
    }

    if (tui) tui->clear();

    auto next_render = std::chrono::steady_clock::now();

    while (g_running) {
        const auto now = std::chrono::steady_clock::now();

        if (terminal::resized()) g_force_refresh = true;

        if (now >= next_render || g_force_refresh.exchange(false)) {
            collect_and_render();
            next_render = std::chrono::steady_clock::now() +
                          std::chrono::seconds(cfg.refresh_interval);
        }

        if (raw_mode) {
            const terminal::KeyEvent ev = terminal::read_key_event();
            if (ev) {
                handle_key(ev, cfg, view, g_running, g_force_refresh);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (tui) tui->show_cursor();
    terminal::shutdown();

    std::cout << "\n\033[0m" << std::flush;
    return 0;
}
