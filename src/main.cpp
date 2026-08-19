/**
 * @file main.cpp
 * @brief sysmon – A comprehensive, cross-platform terminal system monitor.
 */

#include "sysmon/system_monitor.hpp"
#include "sysmon/cpu_monitor.hpp"
#include "sysmon/memory_monitor.hpp"
#include "sysmon/gpu_monitor.hpp"
#include "sysmon/load_monitor.hpp"
#include "sysmon/disk_monitor.hpp"
#include "sysmon/disk_io_monitor.hpp"
#include "sysmon/temperature_monitor.hpp"
#include "sysmon/network_monitor.hpp"
#include "sysmon/net_connections_monitor.hpp"
#include "sysmon/process_monitor.hpp"
#include "sysmon/config.hpp"
#include "sysmon/tui.hpp"
#include "sysmon/text_renderer.hpp"

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory>

static constexpr const char* VERSION = "0.2.0";

// ---------------------------------------------------------------------------
// Signal / keyboard handling
// ---------------------------------------------------------------------------

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_force_refresh{false};

static void signal_handler(int) {
    g_running = false;
}

static void resize_handler(int) {
    g_force_refresh = true;
}

static void setup_terminal_raw(struct termios& old_tio) {
    struct termios new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~static_cast<unsigned long>(ICANON | ECHO);
    new_tio.c_cc[VMIN]  = 0;
    new_tio.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

static void restore_terminal(const struct termios& old_tio) {
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    Config cfg = Config::load();

    bool once_flag_set = false;
    bool no_tui_flag_set = false;

    // Command line argument parser
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            std::cout << "sysmon v" << VERSION << " - Comprehensive System Monitor\n\n"
                      << "Usage:\n"
                      << "  sysmon [options]\n\n"
                      << "Display Options:\n"
                      << "  --once                 Print metrics once and exit\n"
                      << "  --no-tui               Plain text output (no ANSI formatting)\n"
                      << "  --compact, -m          Compact / summary dashboard mode\n"
                      << "  --interval N, -i N     Set update interval in seconds (default: 2)\n"
                      << "  --limit N              Max number of processes to display\n\n"
                      << "Component Visibility Toggles:\n"
                      << "  --cores / --no-cores   Show / hide individual CPU cores\n"
                      << "  --gpu / --no-gpu       Show / hide GPU & VRAM statistics\n"
                      << "  --conn / --no-conn     Show / hide active network connections\n"
                      << "  --proc / --no-proc     Show / hide top processes table\n"
                      << "  --net / --no-net       Show / hide network interfaces\n"
                      << "  --temp / --no-temp     Show / hide temperatures & sensors\n"
                      << "  --disk / --no-disk     Show / hide storage & disk I/O\n\n"
                      << "Configuration:\n"
                      << "  --config PATH          Load configuration from PATH\n"
                      << "  --generate-config      Write default config to ~/.config/sysmon/sysmon.conf\n"
                      << "  --show-config          Print currently active configuration and exit\n"
                      << "  --version, -v          Show version information\n\n"
                      << "Interactive Hotkeys (TUI Mode):\n"
                      << "  [c] Toggle CPU cores    [g] Toggle GPU         [n] Toggle Network\n"
                      << "  [v] Toggle Connections  [p] Toggle Processes   [t] Toggle Temperatures\n"
                      << "  [d] Toggle Storage      [m] Toggle Compact     [s] Save current config\n"
                      << "  [r] Refresh screen      [q] / ESC Quit\n";
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::cout << "sysmon " << VERSION << "\n";
            return 0;
        }
        if (arg == "--generate-config") {
            Config def = Config::defaults();
            def.save();
            std::cout << "Generated default config at: " << Config::default_config_path() << "\n";
            return 0;
        }
        if (arg == "--show-config") {
            std::cout << cfg.to_string();
            return 0;
        }
        if (arg == "--config" && i + 1 < argc) {
            cfg = Config::load_from(argv[++i]);
        }
        if (arg == "--once") {
            once_flag_set = true;
        }
        if (arg == "--no-tui") {
            no_tui_flag_set = true;
        }
        if (arg == "--compact" || arg == "-m") {
            cfg.compact_mode = true;
        }
        if ((arg == "--interval" || arg == "-i") && i + 1 < argc) {
            try {
                cfg.refresh_interval = std::stoi(argv[++i]);
                if (cfg.refresh_interval < 1) cfg.refresh_interval = 1;
            } catch (...) {}
        }
        if (arg == "--limit" && i + 1 < argc) {
            try {
                cfg.proc_limit = std::stoi(argv[++i]);
            } catch (...) {}
        }
        // Component flags
        if (arg == "--cores")      cfg.show_cpu_per_core = true;
        if (arg == "--no-cores")   cfg.show_cpu_per_core = false;
        if (arg == "--gpu")        cfg.show_gpu = true;
        if (arg == "--no-gpu")     cfg.show_gpu = false;
        if (arg == "--conn")       cfg.show_connections = true;
        if (arg == "--no-conn")    cfg.show_connections = false;
        if (arg == "--proc")       cfg.show_processes = true;
        if (arg == "--no-proc")    cfg.show_processes = false;
        if (arg == "--net")        cfg.show_network = true;
        if (arg == "--no-net")     cfg.show_network = false;
        if (arg == "--temp")       cfg.show_temperature = true;
        if (arg == "--no-temp")    cfg.show_temperature = false;
        if (arg == "--disk")       cfg.show_disk = true;
        if (arg == "--no-disk")    cfg.show_disk = false;
    }

    // Auto-detect non-terminal stdout (pipes, redirects)
    bool stdout_is_tty = isatty(STDOUT_FILENO) != 0;
    bool stdin_is_tty  = isatty(STDIN_FILENO) != 0;

    bool is_once = once_flag_set || !cfg.tui_enabled || (!stdout_is_tty && !no_tui_flag_set);
    bool is_no_tui = no_tui_flag_set || !stdout_is_tty;

    // Initialize all monitors
    SystemMonitor          sys_mon;
    CpuMonitor             cpu_mon;
    MemoryMonitor          mem_mon;
    GpuMonitor             gpu_mon;
    LoadMonitor            load_mon;
    DiskMonitor            disk_mon;
    DiskIOMonitor          disk_io_mon;
    TemperatureMonitor     temp_mon;
    NetworkMonitor         net_mon;
    NetConnectionsMonitor  conn_mon;
    ProcessMonitor         proc_mon;

    std::unique_ptr<TUI> tui;
    TextRenderer text_renderer;

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);
#ifdef SIGWINCH
    std::signal(SIGWINCH, resize_handler);
#endif

    struct termios old_tio{};
    bool raw_mode = false;

    if (!is_once && !is_no_tui && stdin_is_tty) {
        setup_terminal_raw(old_tio);
        raw_mode = true;
        tui = std::make_unique<TUI>();
    }

    auto collect_and_render = [&]() {
        try {
            auto system  = sys_mon.read();
            auto cpu     = cpu_mon.read();
            auto memory  = mem_mon.read();
            auto gpus    = cfg.show_gpu ? gpu_mon.read() : std::vector<GpuStats>{};
            auto load    = load_mon.read();
            auto disks   = cfg.show_disk ? disk_mon.read() : std::vector<DiskStats>{};
            auto disk_io = (cfg.show_disk && cfg.show_disk_io) ? disk_io_mon.read() : std::vector<DiskIOStats>{};
            auto temps   = cfg.show_temperature ? temp_mon.read() : TemperatureStats{};
            auto net     = cfg.show_network ? net_mon.read() : std::vector<NetworkStats>{};
            auto conns   = (cfg.show_connections && !cfg.compact_mode)
                           ? conn_mon.read(cfg.connections_show_listen, static_cast<unsigned int>(cfg.connections_limit))
                           : std::vector<NetConnectionStats>{};
            auto procs   = (cfg.show_processes && !cfg.compact_mode)
                           ? proc_mon.read(static_cast<unsigned int>(cfg.proc_limit))
                           : std::vector<ProcessStats>{};

            if (is_once || is_no_tui || !tui) {
                text_renderer.render(system, cpu, memory, gpus, load, disks, disk_io, net, conns, procs, temps, cfg);
            } else {
                tui->render(system, cpu, memory, gpus, load, disks, disk_io, net, conns, procs, temps, cfg);
            }
        } catch (const std::exception& e) {
            if (raw_mode) restore_terminal(old_tio);
            std::cerr << "\nError: " << e.what() << "\n";
            g_running = false;
        }
    };

    // Warm-up sample (initializes delta rate counters for CPU, net, disk, procs)
    try {
        cpu_mon.read();
        net_mon.read();
        disk_io_mon.read();
        proc_mon.read(1);
    } catch (...) {}
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    if (is_once) {
        collect_and_render();
        return 0;
    }

    if (tui) {
        tui->clear();
    }

    auto next_render = std::chrono::steady_clock::now();

    while (g_running) {
        auto now = std::chrono::steady_clock::now();

        if (now >= next_render || g_force_refresh.exchange(false)) {
            collect_and_render();
            next_render = std::chrono::steady_clock::now()
                        + std::chrono::seconds(cfg.refresh_interval);
        }

        // Interactive keyboard handling
        if (raw_mode) {
            char ch = 0;
            ssize_t n = ::read(STDIN_FILENO, &ch, 1);
            if (n > 0) {
                if (ch == 'q' || ch == 'Q' || ch == 27 /* ESC */ || ch == 3 /* Ctrl+C */) {
                    g_running = false;
                } else if (ch == 'r' || ch == 'R') {
                    g_force_refresh = true;
                } else if (ch == 'c' || ch == 'C') {
                    cfg.show_cpu_per_core = !cfg.show_cpu_per_core;
                    g_force_refresh = true;
                } else if (ch == 'g' || ch == 'G') {
                    cfg.show_gpu = !cfg.show_gpu;
                    g_force_refresh = true;
                } else if (ch == 'n' || ch == 'N') {
                    cfg.show_network = !cfg.show_network;
                    g_force_refresh = true;
                } else if (ch == 'v' || ch == 'V') {
                    cfg.show_connections = !cfg.show_connections;
                    g_force_refresh = true;
                } else if (ch == 'p' || ch == 'P') {
                    cfg.show_processes = !cfg.show_processes;
                    g_force_refresh = true;
                } else if (ch == 't' || ch == 'T') {
                    cfg.show_temperature = !cfg.show_temperature;
                    g_force_refresh = true;
                } else if (ch == 'd' || ch == 'D') {
                    cfg.show_disk = !cfg.show_disk;
                    g_force_refresh = true;
                } else if (ch == 'm' || ch == 'M') {
                    cfg.compact_mode = !cfg.compact_mode;
                    g_force_refresh = true;
                } else if (ch == 's' || ch == 'S') {
                    cfg.save();
                    g_force_refresh = true;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (raw_mode) {
        restore_terminal(old_tio);
    }

    if (tui) {
        tui->show_cursor();
    }
    std::cout << "\n\033[0m" << std::flush;
    return 0;
}