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
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

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
        << "  --compact, -m          Compact / summary dashboard mode\n"
        << "  --interval N, -i N     Update interval in seconds (default: 2)\n"
        << "  --limit N              Max number of processes to display\n"
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
        << "  [c] Toggle CPU cores    [g] Toggle GPU         [n] Toggle Network\n"
        << "  [v] Toggle Connections  [p] Toggle Processes   [t] Toggle Temperatures\n"
        << "  [d] Toggle Storage      [b] Toggle Battery     [m] Toggle Compact\n"
        << "  [o] Cycle process sort  [s] Save current config\n"
        << "  [r] Refresh screen      [q] / ESC Quit\n";
}

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
        if (arg == "--compact" || arg == "-m") { cfg.compact_mode = true; continue; }

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
            collect_cfg.compact_mode     = false;
            collect_cfg.connections_show_listen = true;
            // The list limits exist to fit a terminal, so they must not shape a
            // data export either: a stale "proc_limit = 5" in ~/.config would
            // otherwise truncate every snapshot, and a truncated array is
            // indistinguishable from a complete one.  0 means "everything".
            // An explicit --limit is a deliberate request and is still honoured;
            // connections have no CLI override at all, so they are always full.
            if (!limit_flag_set) collect_cfg.proc_limit = 0;
            collect_cfg.connections_limit = 0;
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
        snap.connections = (collect_cfg.show_connections && !collect_cfg.compact_mode)
                         ? conn_mon.read(collect_cfg.connections_show_listen,
                                         static_cast<unsigned int>(collect_cfg.connections_limit))
                         : std::vector<NetConnectionStats>{};
        snap.processes = (collect_cfg.show_processes && !collect_cfg.compact_mode)
                       ? proc_mon.read(static_cast<unsigned int>(collect_cfg.proc_limit),
                                       collect_cfg.proc_sort)
                       : std::vector<ProcessStats>{};

        if (collect_cfg.show_network) {
            snap.net_global = net_mon.read_global();
            NetConnectionsMonitor::summarize(snap.connections, snap.net_global);
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
                tui->render(snap, cfg);
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
            const int key = terminal::read_key();
            if (key > 0) {
                switch (key) {
                    case 'q': case 'Q': case 27: case 3:
                        g_running = false;
                        break;
                    case 'r': case 'R': g_force_refresh = true; break;
                    case 'c': case 'C': cfg.show_cpu_per_core = !cfg.show_cpu_per_core; g_force_refresh = true; break;
                    case 'g': case 'G': cfg.show_gpu         = !cfg.show_gpu;         g_force_refresh = true; break;
                    case 'n': case 'N': cfg.show_network     = !cfg.show_network;     g_force_refresh = true; break;
                    case 'v': case 'V': cfg.show_connections = !cfg.show_connections; g_force_refresh = true; break;
                    case 'p': case 'P': cfg.show_processes   = !cfg.show_processes;   g_force_refresh = true; break;
                    case 't': case 'T': cfg.show_temperature = !cfg.show_temperature; g_force_refresh = true; break;
                    case 'd': case 'D': cfg.show_disk        = !cfg.show_disk;        g_force_refresh = true; break;
                    case 'b': case 'B': cfg.show_battery     = !cfg.show_battery;     g_force_refresh = true; break;
                    case 'm': case 'M': cfg.compact_mode     = !cfg.compact_mode;     g_force_refresh = true; break;
                    case 's': case 'S': cfg.save();                                   g_force_refresh = true; break;
                    case 'o': case 'O': {
                        // Cycle through the process sort orders.
                        switch (cfg.proc_sort) {
                            case ProcSort::Cpu:    cfg.proc_sort = ProcSort::Memory; break;
                            case ProcSort::Memory: cfg.proc_sort = ProcSort::Time;   break;
                            case ProcSort::Time:   cfg.proc_sort = ProcSort::Pid;    break;
                            case ProcSort::Pid:    cfg.proc_sort = ProcSort::Name;   break;
                            case ProcSort::Name:   cfg.proc_sort = ProcSort::Cpu;    break;
                        }
                        g_force_refresh = true;
                        break;
                    }
                    default: break;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (tui) tui->show_cursor();
    terminal::shutdown();

    std::cout << "\n\033[0m" << std::flush;
    return 0;
}
