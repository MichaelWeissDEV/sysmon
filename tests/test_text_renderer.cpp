#include <gtest/gtest.h>
#include "sysmon/text_renderer.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace {

class CoutCapture {
public:
    CoutCapture() : old_buf_(std::cout.rdbuf()) {
        std::cout.rdbuf(oss_.rdbuf());
    }
    ~CoutCapture() {
        std::cout.rdbuf(old_buf_);
    }
    std::string str() const { return oss_.str(); }

private:
    std::streambuf* old_buf_;
    std::ostringstream oss_;
};

CpuStats make_cpu_with_unknown_values() {
    CpuStats cpu;
    cpu.model = "Test CPU";
    cpu.logical_cores = 8;
    cpu.physical_cores = 4;
    cpu.frequency_mhz = std::nullopt;
    cpu.max_frequency_mhz = std::nullopt;
    cpu.temperature_celsius = std::nullopt;
    return cpu;
}

GpuStats make_apple_gpu_unavailable() {
    GpuStats g;
    g.name = "Apple M-Test GPU";
    g.vendor = "Apple";
    g.memory_type = "Unified";
    g.memory_total_bytes = 128ULL * 1024 * 1024 * 1024;
    g.memory_used_bytes = std::nullopt;
    g.memory_free_bytes = std::nullopt;
    g.memory_usage_percent = std::nullopt;
    g.usage_percent = std::nullopt;
    g.frequency_mhz = std::nullopt;
    return g;
}

} // namespace

TEST(TextRendererTest, UnknownCpuValuesRenderedAsNA) {
    CoutCapture cap;
    Config cfg = Config::defaults();
    cfg.show_gpu = false;
    cfg.show_memory = false;
    cfg.show_temperature = false;
    cfg.show_disk = false;
    cfg.show_network = false;
    cfg.show_connections = false;
    cfg.show_processes = false;
    cfg.show_battery = false;

    Snapshot snap;
    snap.cpu = make_cpu_with_unknown_values();

    TextRenderer renderer;
    renderer.render(snap, cfg);

    std::string out = cap.str();
    EXPECT_NE(out.find("Frequency       N/A"), std::string::npos) << out;
    EXPECT_NE(out.find("Temperature     N/A"), std::string::npos) << out;
}

TEST(TextRendererTest, AppleGpuShowsUnifiedCapacityNotFakeUsage) {
    CoutCapture cap;
    Config cfg = Config::defaults();
    cfg.show_cpu = false;
    cfg.show_memory = false;
    cfg.show_temperature = false;
    cfg.show_disk = false;
    cfg.show_network = false;
    cfg.show_connections = false;
    cfg.show_processes = false;
    cfg.show_battery = false;

    Snapshot snap;
    snap.gpus = {make_apple_gpu_unavailable()};

    TextRenderer renderer;
    renderer.render(snap, cfg);

    std::string out = cap.str();
    EXPECT_NE(out.find("Usage       N/A"), std::string::npos) << out;
    EXPECT_NE(out.find("System unified-memory capacity: 128.0 GB"),
              std::string::npos) << out;
    // A fabricated "GPU Memory Used" derived from VM statistics must not appear.
    EXPECT_EQ(out.find("GPU Memory"), std::string::npos) << out;
}

TEST(TextRendererTest, KnownCpuValuesRenderedNumerically) {
    CoutCapture cap;
    Config cfg = Config::defaults();
    cfg.show_gpu = false;
    cfg.show_memory = false;
    cfg.show_temperature = false;
    cfg.show_disk = false;
    cfg.show_network = false;
    cfg.show_connections = false;
    cfg.show_processes = false;
    cfg.show_battery = false;

    CpuStats cpu = make_cpu_with_unknown_values();
    cpu.frequency_mhz = 3200.0;
    cpu.max_frequency_mhz = 3600.0;
    cpu.temperature_celsius = 45.5;

    Snapshot snap;
    snap.cpu = cpu;

    TextRenderer renderer;
    renderer.render(snap, cfg);

    std::string out = cap.str();
    EXPECT_NE(out.find("Frequency       3200 MHz / 3600 MHz max"), std::string::npos) << out;
    EXPECT_NE(out.find("Temperature     45.5 °C"), std::string::npos) << out;
}
namespace {

/// A snapshot with enough of every section filled in to render all of them.
Snapshot make_full_snapshot() {
    Snapshot snap;

    snap.system.hostname     = "testbox";
    snap.system.os           = "Test OS 1.0";
    snap.system.architecture = "arm64";
    snap.system.kernel       = "test-kernel";
    snap.system.uptime       = "1h 2m 3s";
    snap.system.page_size_bytes = 16384;

    snap.cpu.model          = "Test CPU";
    snap.cpu.logical_cores  = 8;
    snap.cpu.physical_cores = 4;
    snap.cpu.usage_percent  = 42.5;
    snap.cpu.per_core.resize(2);
    snap.cpu.per_core[0].id = 0;
    snap.cpu.per_core[1].id = 1;

    snap.memory.ram_total_bytes   = 32ULL * 1024 * 1024 * 1024;
    snap.memory.ram_used_bytes    = 8ULL  * 1024 * 1024 * 1024;
    snap.memory.ram_usage_percent = 25.0;
    snap.memory.swap_total_bytes  = 4ULL * 1024 * 1024 * 1024;
    snap.memory.swap_used_bytes   = 1024;

    snap.load.load_1min         = 1.5;
    snap.load.load_5min         = 1.25;
    snap.load.load_15min        = 1.0;
    snap.load.total_processes   = 300;
    snap.load.running_processes = 2;
    snap.load.total_threads     = 1200;

    DiskStats disk;
    disk.mountpoint      = "/";
    disk.device          = "/dev/testdisk";
    disk.filesystem_type = "testfs";
    disk.total_bytes     = 1000ULL * 1000 * 1000 * 1000;
    disk.used_bytes      = 400ULL  * 1000 * 1000 * 1000;
    disk.available_bytes = 600ULL  * 1000 * 1000 * 1000;
    disk.usage_percent   = 40.0;
    snap.disks.push_back(disk);

    NetworkStats iface;
    iface.name             = "eth0";
    iface.ip_address       = "192.0.2.10";
    iface.mac_address      = "aa:bb:cc:dd:ee:ff";
    iface.mtu              = 1500;
    iface.rx_bytes_total   = 1024;
    iface.tx_bytes_total   = 2048;
    iface.rx_bytes_per_sec = 100.0;
    iface.tx_bytes_per_sec = 200.0;
    iface.is_up            = true;
    snap.network.push_back(iface);
    snap.net_global.default_gateway_v4 = "192.0.2.1";
    snap.net_global.tcp_established    = 7;

    NetConnectionStats conn;
    conn.protocol     = "tcp4";
    conn.local_addr   = "192.0.2.10";
    conn.local_port   = 1234;
    conn.remote_addr  = "198.51.100.5";
    conn.remote_port  = 443;
    conn.state        = "ESTABLISHED";
    conn.pid          = 4242;
    conn.process_name = "testproc";
    snap.connections.push_back(conn);

    ProcessStats proc;
    proc.pid                    = 4242;
    proc.ppid                   = 1;
    proc.name                   = "testproc";
    proc.cmdline                = "/usr/bin/testproc --flag value";
    proc.user                   = "tester";
    proc.state                  = "S";
    proc.cpu_percent            = 12.5;
    proc.cpu_time_seconds       = 65.0;
    proc.mem_rss_bytes          = 100ULL * 1024 * 1024;
    proc.mem_vms_bytes          = 500ULL * 1024 * 1024;
    proc.mem_percent            = 1.25;
    proc.threads                = 9;
    proc.nice                   = 5;
    proc.open_files             = 33;
    proc.io_read_bytes_per_sec  = 1024.0;
    proc.io_write_bytes_per_sec = 2048.0;
    proc.socket_count           = 3u;
    snap.processes.push_back(proc);

    snap.battery.present = true;
    snap.battery.percent = 88.0;
    snap.battery.state   = "Discharging";

    return snap;
}

std::string render(const Snapshot& snap, const Config& cfg) {
    std::ostringstream out;
    TextRenderer renderer;
    renderer.render_to(out, snap, cfg);
    return out.str();
}

std::size_t line_count(const std::string& text) {
    return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
}

} // namespace

TEST(TextRendererDetailTest, CompactLevelIsASummaryNotTheFullReport) {
    // --compact used to be accepted and then ignored here, so the flag looked
    // as though it had worked while the output was byte-identical.
    const Snapshot snap = make_full_snapshot();

    Config normal = Config::defaults();
    Config compact = Config::defaults();
    compact.detail_level = DetailLevel::Compact;

    const std::string full  = render(snap, normal);
    const std::string brief = render(snap, compact);

    EXPECT_NE(full, brief) << "compact produced identical output";
    EXPECT_LT(line_count(brief), line_count(full));

    // The summary still has to answer the headline questions.
    for (const char* needle : {"testbox", "CPU", "RAM", "Processes"}) {
        EXPECT_NE(brief.find(needle), std::string::npos) << needle << " missing:\n" << brief;
    }
    // ...and must not drag the per-process table along.
    EXPECT_EQ(brief.find("COMMAND"), std::string::npos) << brief;
}

TEST(TextRendererDetailTest, EachLevelIsAtLeastAsDetailedAsTheOneBelow) {
    const Snapshot snap = make_full_snapshot();

    std::size_t previous = 0;
    for (const DetailLevel level : {DetailLevel::Compact, DetailLevel::Normal,
                                    DetailLevel::Detailed, DetailLevel::Full}) {
        Config cfg = Config::defaults();
        cfg.detail_level = level;
        const std::size_t lines = line_count(render(snap, cfg));
        EXPECT_GE(lines, previous) << Config::detail_name(level)
                                   << " printed fewer lines than the level below it";
        previous = lines;
    }
}

TEST(TextRendererDetailTest, DetailedLevelAddsTheColumnsNormalOmits) {
    const Snapshot snap = make_full_snapshot();

    Config normal = Config::defaults();
    Config detailed = Config::defaults();
    detailed.detail_level = DetailLevel::Detailed;

    const std::string plain = render(snap, normal);
    const std::string rich  = render(snap, detailed);

    for (const char* column : {"NICE", "FDS", "DISK R", "DISK W", "NET TX", "SOCK"}) {
        EXPECT_EQ(plain.find(column), std::string::npos)
            << column << " leaked into the normal table";
        EXPECT_NE(rich.find(column), std::string::npos)
            << column << " missing from the detailed table";
    }
    EXPECT_NE(rich.find("Page size"), std::string::npos) << rich;
}

TEST(TextRendererDetailTest, FullLevelPrintsCommandLines) {
    const Snapshot snap = make_full_snapshot();

    Config detailed = Config::defaults();
    detailed.detail_level = DetailLevel::Detailed;
    Config full = Config::defaults();
    full.detail_level = DetailLevel::Full;

    EXPECT_EQ(render(snap, detailed).find("/usr/bin/testproc --flag value"), std::string::npos);
    EXPECT_NE(render(snap, full).find("/usr/bin/testproc --flag value"), std::string::npos);
    EXPECT_NE(render(snap, full).find("Command lines"), std::string::npos);
}

TEST(TextRendererLimitTest, ZeroLimitPrintsEveryRowRatherThanNone) {
    // A limit of 0 means "no limit" in every collector; the display loops used
    // to read it as a count and print nothing at all.
    Snapshot snap = make_full_snapshot();
    for (int i = 0; i < 5; ++i) {
        ProcessStats extra = snap.processes.front();
        extra.pid  = 5000 + i;
        extra.name = "extra" + std::to_string(i);
        snap.processes.push_back(extra);
        NetConnectionStats conn = snap.connections.front();
        conn.local_port = static_cast<uint16_t>(20000 + i);
        snap.connections.push_back(conn);
    }

    Config cfg = Config::defaults();
    cfg.proc_limit        = 0;
    cfg.connections_limit = 0;
    const std::string out = render(snap, cfg);

    EXPECT_NE(out.find("Processes (all 6)"), std::string::npos) << out;
    EXPECT_NE(out.find("extra4"), std::string::npos) << "the last process was dropped";
    EXPECT_NE(out.find("Active Network Connections (all 6)"), std::string::npos) << out;
}

TEST(TextRendererLimitTest, APositiveLimitStillTruncates) {
    Snapshot snap = make_full_snapshot();
    for (int i = 0; i < 5; ++i) {
        ProcessStats extra = snap.processes.front();
        extra.pid  = 6000 + i;
        extra.name = "surplus" + std::to_string(i);
        snap.processes.push_back(extra);
    }

    Config cfg = Config::defaults();
    cfg.proc_limit = 2;
    const std::string out = render(snap, cfg);

    EXPECT_NE(out.find("Processes (top 2)"), std::string::npos) << out;
    EXPECT_EQ(out.find("surplus4"), std::string::npos) << "the limit was not applied";
}

TEST(TextRendererTest, UnmeasurableProcessFieldsRenderAsNA) {
    // The optionality contract has to reach the text renderer too: a process
    // whose network use could not be attributed must not read as a zero.
    Snapshot snap = make_full_snapshot();
    ProcessStats& p = snap.processes.front();
    p.nice                   = std::nullopt;
    p.open_files             = std::nullopt;
    p.io_read_bytes_per_sec  = std::nullopt;
    p.io_write_bytes_per_sec = std::nullopt;
    p.tx_bytes_per_sec       = std::nullopt;
    p.socket_count           = std::nullopt;

    Config cfg = Config::defaults();
    cfg.detail_level = DetailLevel::Detailed;
    const std::string out = render(snap, cfg);

    // Assert on the process row itself.  Searching the whole document would
    // match "0.0 B/s" inside a legitimate "100.0 B/s" elsewhere, and a test
    // that can be satisfied by an unrelated line is not checking this one.
    const std::size_t row_start = out.find("  testproc");
    ASSERT_NE(row_start, std::string::npos) << out;
    const std::size_t row_end = out.find('\n', row_start);
    const std::string row = out.substr(row_start, row_end - row_start);

    std::size_t na_count = 0;
    for (std::size_t pos = row.find("N/A"); pos != std::string::npos;
         pos = row.find("N/A", pos + 3)) {
        ++na_count;
    }
    EXPECT_GE(na_count, 6u)
        << "unmeasured process fields did not render as N/A:\n|" << row << "|";
    EXPECT_EQ(row.find(" 0.0 B/s"), std::string::npos)
        << "an unmeasured rate was rendered as a zero:\n|" << row << "|";
    EXPECT_EQ(row.find(" 0 "), std::string::npos)
        << "an unmeasured count was rendered as a zero:\n|" << row << "|";
}
