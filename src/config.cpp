#include "sysmon/config.hpp"
#include "sysmon/stats.hpp"
#include "sysmon/utils.hpp"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <cstdlib>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

std::string Config::default_config_path() {
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";
    return std::string(home) + "/.config/sysmon/sysmon.conf";
}

// ---------------------------------------------------------------------------
// Parser helpers
// ---------------------------------------------------------------------------

static std::string strip_comment(const std::string& line) {
    auto pos = line.find('#');
    return pos != std::string::npos ? line.substr(0, pos) : line;
}

static bool parse_bool(const std::string& v) {
    std::string lv = v;
    std::transform(lv.begin(), lv.end(), lv.begin(), ::tolower);
    return lv == "true" || lv == "1" || lv == "yes" || lv == "on";
}

static std::set<std::string> parse_set(const std::string& v) {
    std::set<std::string> result;
    // Strip surrounding brackets/quotes
    std::string s = v;
    s.erase(std::remove_if(s.begin(), s.end(), [](char c){ return c=='"'||c=='['||c==']'; }), s.end());
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, ',')) {
        std::string t = utils::trim(token);
        if (!t.empty()) result.insert(t);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Config::load / load_from
// ---------------------------------------------------------------------------

Config Config::load() {
    return load_from(default_config_path());
}

Config Config::load_from(const std::string& path) {
    Config cfg = defaults();

    std::ifstream file(path);
    if (!file.is_open()) return cfg;

    std::string section;
    std::string line;

    while (std::getline(file, line)) {
        line = utils::trim(strip_comment(line));
        if (line.empty()) continue;

        // Section header
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = utils::trim(line.substr(0, eq));
        std::string val = utils::trim(line.substr(eq + 1));

        // Remove inline comments
        auto cm = val.find('#');
        if (cm != std::string::npos) val = utils::trim(val.substr(0, cm));

        // ---- [display] ----
        if (section == "display") {
            if (key == "refresh_interval")           cfg.refresh_interval = std::stoi(val);
            else if (key == "tui_enabled")           cfg.tui_enabled = parse_bool(val);
            else if (key == "compact_mode")          cfg.compact_mode = parse_bool(val);
            else if (key == "show_cpu")              cfg.show_cpu = parse_bool(val);
            else if (key == "show_cpu_per_core")     cfg.show_cpu_per_core = parse_bool(val);
            else if (key == "show_cpu_cores_detail") cfg.show_cpu_cores_detail = parse_bool(val);
            else if (key == "show_memory")           cfg.show_memory = parse_bool(val);
            else if (key == "show_swap")             cfg.show_swap = parse_bool(val);
            else if (key == "show_memory_cache")     cfg.show_memory_cache = parse_bool(val);
            else if (key == "show_gpu")              cfg.show_gpu = parse_bool(val);
            else if (key == "show_gpu_memory")       cfg.show_gpu_memory = parse_bool(val);
            else if (key == "show_temperature")      cfg.show_temperature = parse_bool(val);
            else if (key == "show_temperature_per_sensor") cfg.show_temperature_per_sensor = parse_bool(val);
            else if (key == "show_disk")             cfg.show_disk = parse_bool(val);
            else if (key == "show_disk_io")          cfg.show_disk_io = parse_bool(val);
            else if (key == "show_network")          cfg.show_network = parse_bool(val);
            else if (key == "show_network_per_iface") cfg.show_network_per_iface = parse_bool(val);
            else if (key == "show_network_sparkline") cfg.show_network_sparkline = parse_bool(val);
            else if (key == "show_connections")      cfg.show_connections = parse_bool(val);
            else if (key == "connections_limit")     cfg.connections_limit = std::stoi(val);
            else if (key == "connections_show_listen") cfg.connections_show_listen = parse_bool(val);
            else if (key == "show_processes")        cfg.show_processes = parse_bool(val);
            else if (key == "proc_limit")            cfg.proc_limit = std::stoi(val);
            else if (key == "show_proc_threads")     cfg.show_proc_threads = parse_bool(val);
            else if (key == "show_proc_network")     cfg.show_proc_network = parse_bool(val);
        }
        // ---- [tui] ----
        else if (section == "tui") {
            if (key == "use_unicode")        cfg.tui_use_unicode = parse_bool(val);
            else if (key == "use_colors")    cfg.tui_use_colors = parse_bool(val);
            else if (key == "sparkline_length") cfg.sparkline_length = std::stoi(val);
            else if (key == "proc_sort_col") cfg.proc_sort_col = std::stoi(val);
        }
        // ---- [network] ----
        else if (section == "network") {
            if (key == "exclude_interfaces") cfg.excluded_interfaces = parse_set(val);
        }
        // ---- [disk] ----
        else if (section == "disk") {
            if (key == "exclude_filesystems") cfg.excluded_filesystems = parse_set(val);
        }
        // ---- [temperature] ----
        else if (section == "temperature") {
            if (key == "exclude_sensors") cfg.excluded_sensors = parse_set(val);
        }
    }

    return cfg;
}

// ---------------------------------------------------------------------------
// Config::save / save_to
// ---------------------------------------------------------------------------

static std::string bool_str(bool v) { return v ? "true" : "false"; }

static std::string set_str(const std::set<std::string>& s) {
    std::string r;
    for (const auto& v : s) {
        if (!r.empty()) r += ", ";
        r += v;
    }
    return r;
}

void Config::save() const {
    save_to(default_config_path());
}

void Config::save_to(const std::string& path) const {
    // Create parent directories
    fs::path p(path);
    fs::create_directories(p.parent_path());

    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "sysmon: could not write config to " << path << "\n";
        return;
    }

    f << "# sysmon configuration file\n"
      << "# Edit this file to customize what sysmon displays.\n"
      << "# Lines starting with # are comments.\n\n";

    f << "[display]\n"
      << "refresh_interval = " << refresh_interval << "\n"
      << "tui_enabled = " << bool_str(tui_enabled) << "\n"
      << "compact_mode = " << bool_str(compact_mode) << "\n\n"
      << "# CPU\n"
      << "show_cpu = " << bool_str(show_cpu) << "\n"
      << "show_cpu_per_core = " << bool_str(show_cpu_per_core) << "\n"
      << "show_cpu_cores_detail = " << bool_str(show_cpu_cores_detail) << "\n\n"
      << "# Memory\n"
      << "show_memory = " << bool_str(show_memory) << "\n"
      << "show_swap = " << bool_str(show_swap) << "\n"
      << "show_memory_cache = " << bool_str(show_memory_cache) << "\n\n"
      << "# GPU\n"
      << "show_gpu = " << bool_str(show_gpu) << "\n"
      << "show_gpu_memory = " << bool_str(show_gpu_memory) << "\n\n"
      << "# Temperature\n"
      << "show_temperature = " << bool_str(show_temperature) << "\n"
      << "show_temperature_per_sensor = " << bool_str(show_temperature_per_sensor) << "\n\n"
      << "# Disk\n"
      << "show_disk = " << bool_str(show_disk) << "\n"
      << "show_disk_io = " << bool_str(show_disk_io) << "\n\n"
      << "# Network\n"
      << "show_network = " << bool_str(show_network) << "\n"
      << "show_network_per_iface = " << bool_str(show_network_per_iface) << "\n"
      << "show_network_sparkline = " << bool_str(show_network_sparkline) << "\n\n"
      << "# Connections\n"
      << "show_connections = " << bool_str(show_connections) << "\n"
      << "connections_limit = " << connections_limit << "\n"
      << "connections_show_listen = " << bool_str(connections_show_listen) << "\n\n"
      << "# Processes\n"
      << "show_processes = " << bool_str(show_processes) << "\n"
      << "proc_limit = " << proc_limit << "\n"
      << "show_proc_threads = " << bool_str(show_proc_threads) << "\n"
      << "show_proc_network = " << bool_str(show_proc_network) << "\n\n";

    f << "[tui]\n"
      << "use_unicode = " << bool_str(tui_use_unicode) << "\n"
      << "use_colors = " << bool_str(tui_use_colors) << "\n"
      << "sparkline_length = " << sparkline_length << "\n"
      << "proc_sort_col = " << proc_sort_col << "\n\n";

    f << "[network]\n"
      << "# Comma-separated interface names to hide (e.g. lo, lo0, docker0)\n"
      << "exclude_interfaces = " << set_str(excluded_interfaces) << "\n\n";

    f << "[disk]\n"
      << "# Comma-separated filesystem types to hide (e.g. tmpfs, devtmpfs)\n"
      << "exclude_filesystems = " << set_str(excluded_filesystems) << "\n\n";

    f << "[temperature]\n"
      << "# Comma-separated sensor name substrings to hide\n"
      << "exclude_sensors = " << set_str(excluded_sensors) << "\n";
}

// ---------------------------------------------------------------------------
// Config::defaults
// ---------------------------------------------------------------------------

Config Config::defaults() {
    Config c;
    c.excluded_interfaces   = {"lo", "lo0"};
    c.excluded_filesystems  = {};
    c.excluded_sensors      = {};
    return c;
}

// ---------------------------------------------------------------------------
// Config::to_string
// ---------------------------------------------------------------------------

std::string Config::to_string() const {
    std::ostringstream oss;
    oss << "Config {\n"
        << "  refresh_interval         = " << refresh_interval << "\n"
        << "  tui_enabled              = " << bool_str(tui_enabled) << "\n"
        << "  compact_mode             = " << bool_str(compact_mode) << "\n"
        << "  show_cpu                 = " << bool_str(show_cpu) << "\n"
        << "  show_cpu_per_core        = " << bool_str(show_cpu_per_core) << "\n"
        << "  show_memory              = " << bool_str(show_memory) << "\n"
        << "  show_swap                = " << bool_str(show_swap) << "\n"
        << "  show_gpu                 = " << bool_str(show_gpu) << "\n"
        << "  show_temperature         = " << bool_str(show_temperature) << "\n"
        << "  show_disk                = " << bool_str(show_disk) << "\n"
        << "  show_disk_io             = " << bool_str(show_disk_io) << "\n"
        << "  show_network             = " << bool_str(show_network) << "\n"
        << "  show_connections         = " << bool_str(show_connections) << "\n"
        << "  show_processes           = " << bool_str(show_processes) << "\n"
        << "  proc_limit               = " << proc_limit << "\n"
        << "  connections_limit        = " << connections_limit << "\n"
        << "}\n";
    return oss.str();
}

// ---------------------------------------------------------------------------
// Config::to_display_flags
// ---------------------------------------------------------------------------

DisplayFlags Config::to_display_flags() const {
    DisplayFlags f;
    f.cpu                   = show_cpu;
    f.cpu_per_core          = show_cpu_per_core;
    f.cpu_cores_detail      = show_cpu_cores_detail;
    f.memory                = show_memory;
    f.swap                  = show_swap;
    f.gpu                   = show_gpu;
    f.gpu_memory            = show_gpu_memory;
    f.temperature           = show_temperature;
    f.temperature_per_sensor = show_temperature_per_sensor;
    f.disk                  = show_disk;
    f.disk_io               = show_disk_io;
    f.network               = show_network;
    f.network_per_iface     = show_network_per_iface;
    f.connections           = show_connections;
    f.processes             = show_processes;
    f.compact               = compact_mode;
    f.proc_limit            = proc_limit;
    return f;
}
