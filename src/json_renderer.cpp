#include "sysmon/json_renderer.hpp"
#include "sysmon/platform.hpp"
#include "sysmon/utils.hpp"
#include "sysmon/version.hpp"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

/// Minimal JSON writer: tracks nesting so the caller does not have to think
/// about commas, and centralises the escaping and null handling rules.
class JsonWriter {
public:
    JsonWriter(std::ostream& out, bool pretty) : out_(out), pretty_(pretty) {}

    void begin_object() { punctuate(); out_ << '{'; wrote_anything_ = true; ++depth_; fresh_ = true; }
    void end_object()   { --depth_; newline(); out_ << '}'; fresh_ = false; }
    void begin_array()  { punctuate(); out_ << '['; wrote_anything_ = true; ++depth_; fresh_ = true; }
    void end_array()    { --depth_; newline(); out_ << ']'; fresh_ = false; }

    /// Emit a bare array element (no key), handling the separating comma.
    void element(const std::string& text) {
        punctuate();
        out_ << '"' << utils::json_escape(text) << '"';
        fresh_ = false;
    }

    void key(const std::string& name) {
        punctuate();
        out_ << '"' << utils::json_escape(name) << "\":";
        if (pretty_) out_ << ' ';
        pending_value_ = true;
    }

    void value(const std::string& text) {
        out_ << '"' << utils::json_escape(text) << '"';
        pending_value_ = false;
        fresh_ = false;
    }

    void value(const char* text) { value(std::string(text)); }
    void value(bool flag)        { out_ << (flag ? "true" : "false"); finish(); }
    void value(uint64_t number)  { out_ << number; finish(); }
    void value(int number)       { out_ << number; finish(); }
    void value(long long number) { out_ << number; finish(); }
    void value(unsigned int n)   { out_ << n; finish(); }

    void value(double number, int precision = 2) {
        // NaN and infinity are not representable in JSON.
        if (!std::isfinite(number)) {
            out_ << "null";
        } else {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(precision) << number;
            out_ << oss.str();
        }
        finish();
    }

    void null() { out_ << "null"; finish(); }

    /// Emit an optional as either its value or `null`.
    template <typename T>
    void value_opt(const std::optional<T>& v, int precision = 2) {
        if (!v.has_value()) { null(); return; }
        if constexpr (std::is_floating_point_v<T>) value(static_cast<double>(*v), precision);
        else if constexpr (std::is_same_v<T, std::string>) value(*v);
        else value(static_cast<uint64_t>(*v));
    }

    /// Emit a string, or `null` when it is empty.
    void value_or_null(const std::string& text) {
        if (text.empty()) { null(); return; }
        value(text);
    }

    template <typename T>
    void field(const std::string& name, const T& v) { key(name); value(v); }

    void field(const std::string& name, double v, int precision) {
        key(name);
        value(v, precision);
    }

    template <typename T>
    void field_opt(const std::string& name, const std::optional<T>& v, int precision = 2) {
        key(name);
        value_opt(v, precision);
    }

    void field_or_null(const std::string& name, const std::string& text) {
        key(name);
        value_or_null(text);
    }

private:
    void finish() { pending_value_ = false; fresh_ = false; }

    void punctuate() {
        if (pending_value_) { pending_value_ = false; return; }
        if (!fresh_) out_ << ',';
        newline();
        fresh_ = false;
    }

    void newline() {
        if (!pretty_) return;
        // No leading blank line before the opening brace of the document.
        if (!wrote_anything_) return;
        out_ << '\n';
        for (int i = 0; i < depth_; ++i) out_ << "  ";
    }

    std::ostream& out_;
    bool pretty_;
    int  depth_{0};
    bool fresh_{true};
    bool pending_value_{false};
    bool wrote_anything_{false};
};

void write_cpu(JsonWriter& w, const CpuStats& s) {
    w.key("cpu");
    w.begin_object();
    w.field("model", s.model);
    w.field_or_null("vendor", s.vendor);
    w.field("logical_cores", s.logical_cores);
    w.field("physical_cores", s.physical_cores);
    w.field_opt("sockets", s.sockets);
    w.field_opt("threads_per_core", s.threads_per_core);
    w.field_opt("performance_cores", s.performance_cores);
    w.field_opt("efficiency_cores", s.efficiency_cores);
    w.field("usage_percent", s.usage_percent, 2);
    w.field("user_percent", s.user_percent, 2);
    w.field("system_percent", s.system_percent, 2);
    w.field("idle_percent", s.idle_percent, 2);
    w.field("iowait_percent", s.iowait_percent, 2);
    w.field("nice_percent", s.nice_percent, 2);
    w.field("irq_percent", s.irq_percent, 2);
    w.field("steal_percent", s.steal_percent, 2);
    w.field_opt("frequency_mhz", s.frequency_mhz, 1);
    w.field_opt("min_frequency_mhz", s.min_frequency_mhz, 1);
    w.field_opt("max_frequency_mhz", s.max_frequency_mhz, 1);
    w.field_opt("base_frequency_mhz", s.base_frequency_mhz, 1);
    w.field_opt("temperature_celsius", s.temperature_celsius, 1);
    w.field_or_null("thermal_pressure", s.thermal_pressure);
    w.field_opt("cache_l1d_bytes", s.cache_l1d_bytes);
    w.field_opt("cache_l1i_bytes", s.cache_l1i_bytes);
    w.field_opt("cache_l2_bytes", s.cache_l2_bytes);
    w.field_opt("cache_l3_bytes", s.cache_l3_bytes);
    w.field_opt("context_switches_per_sec", s.context_switches_per_sec, 1);
    w.field_opt("interrupts_per_sec", s.interrupts_per_sec, 1);
    w.field_opt("forks_per_sec", s.forks_per_sec, 1);

    w.key("flags");
    w.begin_array();
    for (const auto& flag : s.flags) w.element(flag);
    w.end_array();

    w.key("per_core");
    w.begin_array();
    for (const auto& c : s.per_core) {
        w.begin_object();
        w.field("id", c.id);
        w.field("usage_percent", c.usage_percent, 2);
        w.field("user_percent", c.user_percent, 2);
        w.field("system_percent", c.system_percent, 2);
        w.field("idle_percent", c.idle_percent, 2);
        w.field_opt("frequency_mhz", c.frequency_mhz, 1);
        w.field_opt("temperature_celsius", c.temperature_celsius, 1);
        w.field_or_null("cluster", c.cluster);
        w.end_object();
    }
    w.end_array();
    w.end_object();
}

} // namespace

void JsonRenderer::render(const Snapshot& snap, const Config& cfg) {
    render_to(std::cout, snap, cfg);
}

std::string JsonRenderer::to_string(const Snapshot& snap, const Config& cfg) {
    std::ostringstream oss;
    render_to(oss, snap, cfg);
    return oss.str();
}

void JsonRenderer::render_to(std::ostream& out, const Snapshot& snap, const Config& cfg) {
    JsonWriter w(out, pretty_);

    w.begin_object();
    w.field("sysmon_version", SYSMON_VERSION);
    w.field("platform", SYSMON_PLATFORM_NAME);

    // -- system ------------------------------------------------------------
    {
        const auto& s = snap.system;
        w.key("system");
        w.begin_object();
        w.field("hostname", s.hostname);
        w.field("os", s.os);
        w.field_or_null("os_build", s.os_build);
        w.field("kernel", s.kernel);
        w.field("architecture", s.architecture);
        w.field_or_null("machine_model", s.machine_model);
        w.field("uptime_seconds", s.uptime_seconds, 0);
        w.field("uptime", s.uptime);
        w.field_or_null("boot_time", s.boot_time);
        w.field_or_null("current_time", s.current_time);
        w.field_or_null("timezone", s.timezone);
        w.field_or_null("virtualization", s.virtualization);
        w.field_opt("logged_in_users", s.logged_in_users);
        w.field_opt("process_count", s.process_count);
        w.field_opt("thread_count", s.thread_count);
        w.field_opt("page_size_bytes", s.page_size_bytes);
        w.end_object();
    }

    write_cpu(w, snap.cpu);

    // -- memory ------------------------------------------------------------
    {
        const auto& s = snap.memory;
        w.key("memory");
        w.begin_object();
        w.field("ram_total_bytes", s.ram_total_bytes);
        w.field("ram_used_bytes", s.ram_used_bytes);
        w.field("ram_available_bytes", s.ram_available_bytes);
        w.field("ram_free_bytes", s.ram_free_bytes);
        w.field("ram_cached_bytes", s.ram_cached_bytes);
        w.field("ram_buffer_bytes", s.ram_buffer_bytes);
        w.field("ram_usage_percent", s.ram_usage_percent, 2);
        w.field("swap_total_bytes", s.swap_total_bytes);
        w.field("swap_used_bytes", s.swap_used_bytes);
        w.field("swap_usage_percent", s.swap_usage_percent, 2);
        w.field_opt("active_bytes", s.active_bytes);
        w.field_opt("inactive_bytes", s.inactive_bytes);
        w.field_opt("wired_bytes", s.wired_bytes);
        w.field_opt("compressed_bytes", s.compressed_bytes);
        w.field_opt("shared_bytes", s.shared_bytes);
        w.field_opt("slab_bytes", s.slab_bytes);
        w.field_opt("dirty_bytes", s.dirty_bytes);
        w.field_opt("commit_total_bytes", s.commit_total_bytes);
        w.field_opt("commit_limit_bytes", s.commit_limit_bytes);
        w.field_opt("page_faults_per_sec", s.page_faults_per_sec, 1);
        w.field_opt("major_faults_per_sec", s.major_faults_per_sec, 1);
        w.field_opt("page_ins_per_sec", s.page_ins_per_sec, 1);
        w.field_opt("page_outs_per_sec", s.page_outs_per_sec, 1);
        w.field_opt("swap_ins_per_sec", s.swap_ins_per_sec, 1);
        w.field_opt("swap_outs_per_sec", s.swap_outs_per_sec, 1);
        w.field_opt("pressure_percent", s.pressure_percent, 1);
        w.end_object();
    }

    // -- load --------------------------------------------------------------
    {
        const auto& s = snap.load;
        w.key("load");
        w.begin_object();
        w.field("load_1min", s.load_1min, 2);
        w.field("load_5min", s.load_5min, 2);
        w.field("load_15min", s.load_15min, 2);
        w.field_opt("load_per_core_1min", s.load_per_core_1min, 3);
        w.field("running_processes", s.running_processes);
        w.field("sleeping_processes", s.sleeping_processes);
        w.field("stopped_processes", s.stopped_processes);
        w.field("zombie_processes", s.zombie_processes);
        w.field("total_processes", s.total_processes);
        w.field("total_threads", s.total_threads);
        w.end_object();
    }

    // -- gpus --------------------------------------------------------------
    w.key("gpus");
    w.begin_array();
    for (const auto& g : snap.gpus) {
        w.begin_object();
        w.field("name", g.name);
        w.field("vendor", g.vendor);
        w.field_or_null("driver_version", g.driver_version);
        w.field_opt("gpu_cores", g.gpu_cores);
        w.field_or_null("memory_type", g.memory_type);
        w.field_opt("memory_total_bytes", g.memory_total_bytes);
        w.field_opt("memory_used_bytes", g.memory_used_bytes);
        w.field_opt("memory_free_bytes", g.memory_free_bytes);
        w.field_opt("memory_usage_percent", g.memory_usage_percent, 2);
        w.field_opt("usage_percent", g.usage_percent, 2);
        w.field_opt("frequency_mhz", g.frequency_mhz, 1);
        w.field_opt("temperature_celsius", g.temperature_celsius, 1);
        w.field_opt("power_watts", g.power_watts, 2);
        w.end_object();
    }
    w.end_array();

    // -- battery -----------------------------------------------------------
    {
        const auto& s = snap.battery;
        w.key("battery");
        w.begin_object();
        w.field("present", s.present);
        w.field("ac_connected", s.ac_connected);
        w.field_or_null("state", s.state);
        w.field_or_null("technology", s.technology);
        w.field_opt("percent", s.percent, 1);
        w.field_opt("time_remaining_minutes", s.time_remaining_minutes, 0);
        w.field_opt("cycle_count", s.cycle_count);
        w.field_opt("health_percent", s.health_percent, 1);
        w.field_opt("temperature_celsius", s.temperature_celsius, 1);
        w.field_opt("voltage_volts", s.voltage_volts, 3);
        w.field_opt("power_watts", s.power_watts, 2);
        w.field_opt("design_capacity_mah", s.design_capacity_mah);
        w.field_opt("full_capacity_mah", s.full_capacity_mah);
        w.field_opt("current_capacity_mah", s.current_capacity_mah);
        w.end_object();
    }

    // -- sensors -----------------------------------------------------------
    {
        const auto& t = snap.temperatures;
        w.key("sensors");
        w.begin_object();
        w.field_opt("cpu_package_celsius", t.cpu_package, 1);
        w.field_opt("hottest_celsius", t.hottest_celsius, 1);
        w.field_or_null("hottest_name", t.hottest_name);

        w.key("temperatures");
        w.begin_array();
        for (const auto& s : t.sensors) {
            w.begin_object();
            w.field("name", s.name);
            w.field_or_null("chip", s.chip);
            w.field("celsius", s.temperature_celsius, 1);
            w.field_opt("high_celsius", s.high, 1);
            w.field_opt("critical_celsius", s.critical, 1);
            w.end_object();
        }
        w.end_array();

        w.key("fans");
        w.begin_array();
        for (const auto& f : t.fans) {
            w.begin_object();
            w.field("name", f.name);
            w.field_or_null("chip", f.chip);
            w.field("rpm", f.rpm, 0);
            w.field_opt("min_rpm", f.min_rpm, 0);
            w.field_opt("max_rpm", f.max_rpm, 0);
            w.end_object();
        }
        w.end_array();
        w.end_object();
    }

    // -- disks -------------------------------------------------------------
    w.key("disks");
    w.begin_array();
    for (const auto& d : snap.disks) {
        w.begin_object();
        w.field("mountpoint", d.mountpoint);
        w.field("device", d.device);
        w.field("filesystem_type", d.filesystem_type);
        w.field("total_bytes", d.total_bytes);
        w.field("used_bytes", d.used_bytes);
        w.field("available_bytes", d.available_bytes);
        w.field("free_bytes", d.free_bytes);
        w.field("usage_percent", d.usage_percent, 2);
        w.field("read_only", d.read_only);
        w.field("removable", d.removable);
        w.field_opt("inodes_total", d.inodes_total);
        w.field_opt("inodes_used", d.inodes_used);
        w.field_opt("inodes_free", d.inodes_free);
        w.field_opt("inode_usage_percent", d.inode_usage_percent, 2);
        w.field_opt("block_size", d.block_size);
        w.field_or_null("mount_options", d.mount_options);
        w.end_object();
    }
    w.end_array();

    w.key("disk_io");
    w.begin_array();
    for (const auto& d : snap.disk_io) {
        w.begin_object();
        w.field("device", d.device);
        w.field("read_bytes_per_sec", d.read_bytes_per_sec, 1);
        w.field("write_bytes_per_sec", d.write_bytes_per_sec, 1);
        w.field("read_ops_per_sec", d.read_ops_per_sec, 1);
        w.field("write_ops_per_sec", d.write_ops_per_sec, 1);
        w.field("read_bytes_total", d.read_bytes_total);
        w.field("write_bytes_total", d.write_bytes_total);
        w.field("read_ops_total", d.read_ops_total);
        w.field("write_ops_total", d.write_ops_total);
        w.field_opt("util_percent", d.util_percent, 1);
        w.field_opt("avg_read_latency_ms", d.avg_read_latency_ms, 3);
        w.field_opt("avg_write_latency_ms", d.avg_write_latency_ms, 3);
        w.field_opt("queue_depth", d.queue_depth, 1);
        w.end_object();
    }
    w.end_array();

    // -- network -----------------------------------------------------------
    w.key("network");
    w.begin_object();
    {
        const auto& g = snap.net_global;
        w.field_or_null("default_gateway_v4", g.default_gateway_v4);
        w.field_or_null("default_gateway_v6", g.default_gateway_v6);
        w.field_or_null("domain", g.domain);
        w.key("dns_servers");
        w.begin_array();
        for (const auto& server : g.dns_servers) w.element(server);
        w.end_array();
        w.field("tcp_established", g.tcp_established);
        w.field("tcp_listen", g.tcp_listen);
        w.field("tcp_time_wait", g.tcp_time_wait);
        w.field("tcp_other", g.tcp_other);
        w.field("udp_sockets", g.udp_sockets);
    }

    w.key("interfaces");
    w.begin_array();
    for (const auto& n : snap.network) {
        w.begin_object();
        w.field("name", n.name);
        w.field_or_null("ip_address", n.ip_address);
        w.field_or_null("ip6_address", n.ip6_address);
        w.field_or_null("netmask", n.netmask);
        w.field_or_null("mac_address", n.mac_address);
        w.field("is_up", n.is_up);
        w.field("is_loopback", n.is_loopback);
        w.field("is_wireless", n.is_wireless);
        w.field_opt("mtu", n.mtu);
        w.field_opt("speed_mbps", n.speed_mbps);
        w.field_or_null("duplex", n.duplex);
        w.field("rx_bytes_total", n.rx_bytes_total);
        w.field("tx_bytes_total", n.tx_bytes_total);
        w.field("rx_packets_total", n.rx_packets_total);
        w.field("tx_packets_total", n.tx_packets_total);
        w.field("rx_errors", n.rx_errors);
        w.field("tx_errors", n.tx_errors);
        w.field("rx_dropped", n.rx_dropped);
        w.field("tx_dropped", n.tx_dropped);
        w.field("rx_bytes_per_sec", n.rx_bytes_per_sec, 1);
        w.field("tx_bytes_per_sec", n.tx_bytes_per_sec, 1);
        w.field("rx_packets_per_sec", n.rx_packets_per_sec, 1);
        w.field("tx_packets_per_sec", n.tx_packets_per_sec, 1);
        w.end_object();
    }
    w.end_array();
    w.end_object();

    // -- connections -------------------------------------------------------
    w.key("connections");
    w.begin_array();
    for (const auto& c : snap.connections) {
        w.begin_object();
        w.field("protocol", c.protocol);
        w.field("local_address", c.local_addr);
        w.field("local_port", static_cast<uint64_t>(c.local_port));
        w.field("remote_address", c.remote_addr);
        w.field("remote_port", static_cast<uint64_t>(c.remote_port));
        w.field("state", c.state);
        w.field("pid", c.pid);
        w.field_or_null("process_name", c.process_name);
        w.end_object();
    }
    w.end_array();

    // -- processes ---------------------------------------------------------
    w.key("processes");
    w.begin_array();
    for (const auto& p : snap.processes) {
        w.begin_object();
        w.field("pid", p.pid);
        w.field("ppid", p.ppid);
        w.field("name", p.name);
        w.field_or_null("cmdline", p.cmdline);
        w.field_or_null("user", p.user);
        w.field_or_null("state", p.state);
        w.field("cpu_percent", p.cpu_percent, 2);
        w.field("cpu_time_seconds", p.cpu_time_seconds, 2);
        w.field("mem_rss_bytes", p.mem_rss_bytes);
        w.field("mem_vms_bytes", p.mem_vms_bytes);
        w.field("mem_percent", p.mem_percent, 3);
        w.field("threads", p.threads);
        w.field("start_time", p.start_time);
        w.field_opt("nice", p.nice);
        w.field_opt("open_files", p.open_files);
        w.field_opt("io_read_bytes_per_sec", p.io_read_bytes_per_sec, 1);
        w.field_opt("io_write_bytes_per_sec", p.io_write_bytes_per_sec, 1);
        w.end_object();
    }
    w.end_array();

    w.end_object();
    out << "\n";

    (void)cfg;
}
