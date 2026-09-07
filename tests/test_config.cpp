#include <gtest/gtest.h>
#include "sysmon/config.hpp"

#include <cstdlib>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

class EnvGuard {
public:
    EnvGuard(const char* name, const char* value) : name_(name), was_set_(false) {
        const char* old = std::getenv(name);
        if (old) {
            old_ = old;
            was_set_ = true;
        }
        if (value) {
            setenv(name, value, 1);
        } else {
            unsetenv(name);
        }
    }
    ~EnvGuard() {
        if (was_set_) {
            setenv(name_.c_str(), old_.c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

private:
    std::string name_;
    std::string old_;
    bool was_set_;
};

std::string make_temp_conf(const std::string& contents) {
    fs::path dir = fs::temp_directory_path() / ("sysmon_test_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    std::string path = (dir / "sysmon.conf").string();
    std::ofstream f(path);
    f << contents;
    f.close();
    return path;
}

} // namespace

TEST(ConfigTest, DefaultsValid) {
    auto cfg = Config::defaults();
    EXPECT_TRUE(cfg.show_cpu);
    EXPECT_TRUE(cfg.show_memory);
    EXPECT_TRUE(cfg.show_gpu);
    EXPECT_TRUE(cfg.show_network);
    EXPECT_GE(cfg.refresh_interval, 1);
}

TEST(ConfigTest, DisplayFlagsConversion) {
    auto cfg = Config::defaults();
    cfg.show_cpu = false;
    cfg.detail_level = DetailLevel::Compact;
    auto flags = cfg.to_display_flags();
    EXPECT_FALSE(flags.cpu);
    EXPECT_TRUE(flags.compact);
}

TEST(ConfigPathTest, XdgConfigHomeTakesPrecedence) {
    EnvGuard xdg("XDG_CONFIG_HOME", "/tmp/xdg-test");
    EnvGuard home("HOME", "/tmp/home-test");
    EXPECT_EQ(Config::default_config_path(), "/tmp/xdg-test/sysmon/sysmon.conf");
}

TEST(ConfigPathTest, HomeFallbackWhenXdgUnset) {
    EnvGuard xdg("XDG_CONFIG_HOME", nullptr);
    EnvGuard home("HOME", "/tmp/home-test");
    EXPECT_EQ(Config::default_config_path(), "/tmp/home-test/.config/sysmon/sysmon.conf");
}

TEST(ConfigPathTest, FallbackWhenHomeUnset) {
    EnvGuard xdg("XDG_CONFIG_HOME", nullptr);
    EnvGuard home("HOME", nullptr);
    EXPECT_EQ(Config::default_config_path(), "/tmp/sysmon/sysmon.conf");
}

TEST(ConfigLoadTest, MissingFileReturnsDefaults) {
    auto cfg = Config::load_from("/nonexistent/path/sysmon.conf");
    EXPECT_TRUE(cfg.show_cpu);
    EXPECT_EQ(cfg.refresh_interval, 2);
}

TEST(ConfigLoadTest, InvalidIntegersDoNotThrow) {
    std::string path = make_temp_conf(
        "[display]\n"
        "refresh_interval = not-a-number\n"
        "proc_limit = 1.5\n"
        "connections_limit = abc\n");
    EXPECT_NO_THROW({
        auto cfg = Config::load_from(path);
        // Invalid values fall back to defaults.
        EXPECT_EQ(cfg.refresh_interval, 2);
    });
}

TEST(ConfigLoadTest, ParsesValidValues) {
    std::string path = make_temp_conf(
        "[display]\n"
        "refresh_interval = 5\n"
        "show_cpu = false\n"
        "show_gpu_memory = on\n"
        "proc_limit = 42\n"
        "[network]\n"
        "exclude_interfaces = lo, docker0\n");
    auto cfg = Config::load_from(path);
    EXPECT_EQ(cfg.refresh_interval, 5);
    EXPECT_FALSE(cfg.show_cpu);
    EXPECT_TRUE(cfg.show_gpu_memory);
    EXPECT_EQ(cfg.proc_limit, 42);
    EXPECT_EQ(cfg.excluded_interfaces.count("lo"), 1u);
    EXPECT_EQ(cfg.excluded_interfaces.count("docker0"), 1u);
}

TEST(ConfigSaveTest, RoundTrip) {
    fs::path dir = fs::temp_directory_path() / ("sysmon_test_save_" + std::to_string(::getpid()));
    std::string path = (dir / "sysmon.conf").string();

    auto cfg = Config::defaults();
    cfg.refresh_interval = 7;
    cfg.show_processes = false;
    cfg.save_to(path);

    EXPECT_TRUE(fs::exists(path));
    auto loaded = Config::load_from(path);
    EXPECT_EQ(loaded.refresh_interval, 7);
    EXPECT_FALSE(loaded.show_processes);
}
// Every persisted field, mutated away from its default and round-tripped.
//
// save_to() and load_from() name their keys in two independent places, so a
// typo in either one loses that setting silently: the user's edit is written,
// never read back, and the value reverts to the default with no error.  Only an
// exhaustive check catches that, so this test must grow with the struct.
//
// Deliberately excluded: show_gpu_per_core, which config.hpp marks as reserved
// for future use and which neither the writer nor the parser handles.
TEST(ConfigSaveTest, EveryPersistedFieldSurvivesRoundTrip) {
    fs::path dir = fs::temp_directory_path() /
                   ("sysmon_test_roundtrip_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const std::string path = (dir / "sysmon.conf").string();

    Config cfg = Config::defaults();

    // [display]
    cfg.refresh_interval            = 11;
    cfg.tui_enabled                 = false;
    cfg.detail_level                = DetailLevel::Detailed;
    cfg.show_cpu                    = false;
    cfg.show_cpu_per_core           = false;
    cfg.show_cpu_cores_detail       = false;
    cfg.show_memory                 = false;
    cfg.show_swap                   = false;
    cfg.show_memory_cache           = false;
    cfg.show_gpu                    = false;
    cfg.show_gpu_memory             = false;
    cfg.show_battery                = false;
    cfg.show_temperature            = false;
    cfg.show_temperature_per_sensor = false;
    cfg.show_disk                   = false;
    cfg.show_disk_io                = false;
    cfg.show_network                = false;
    cfg.show_network_per_iface      = false;
    cfg.show_network_sparkline      = false;
    cfg.show_network_details        = true;
    cfg.show_network_inactive       = true;
    cfg.show_connections            = false;
    cfg.connections_limit           = 13;
    cfg.connections_show_listen     = true;
    cfg.show_processes              = false;
    cfg.proc_limit                  = 17;
    cfg.show_proc_threads           = false;
    cfg.show_proc_network           = true;
    cfg.proc_sort                   = ProcSort::Time;

    cfg.start_view                  = View::Processes;

    // [tui]
    cfg.tui_use_unicode = false;
    cfg.tui_use_colors  = false;
    cfg.sparkline_length = 19;
    cfg.proc_sort_col    = 2;

    // Exclusion sets
    cfg.excluded_interfaces  = {"eth9", "tun7"};
    cfg.excluded_filesystems = {"tmpfs", "nfs"};
    cfg.excluded_sensors     = {"acpitz", "coretemp"};

    cfg.save_to(path);
    const Config got = Config::load_from(path);

    EXPECT_EQ(got.refresh_interval, 11);
    EXPECT_FALSE(got.tui_enabled);
    EXPECT_EQ(got.detail_level, DetailLevel::Detailed);
    EXPECT_FALSE(got.compact_mode());
    EXPECT_FALSE(got.show_cpu);
    EXPECT_FALSE(got.show_cpu_per_core);
    EXPECT_FALSE(got.show_cpu_cores_detail);
    EXPECT_FALSE(got.show_memory);
    EXPECT_FALSE(got.show_swap);
    EXPECT_FALSE(got.show_memory_cache);
    EXPECT_FALSE(got.show_gpu);
    EXPECT_FALSE(got.show_gpu_memory);
    EXPECT_FALSE(got.show_battery);
    EXPECT_FALSE(got.show_temperature);
    EXPECT_FALSE(got.show_temperature_per_sensor);
    EXPECT_FALSE(got.show_disk);
    EXPECT_FALSE(got.show_disk_io);
    EXPECT_FALSE(got.show_network);
    EXPECT_FALSE(got.show_network_per_iface);
    EXPECT_FALSE(got.show_network_sparkline);
    EXPECT_TRUE(got.show_network_details);
    EXPECT_TRUE(got.show_network_inactive);
    EXPECT_FALSE(got.show_connections);
    EXPECT_EQ(got.connections_limit, 13);
    EXPECT_TRUE(got.connections_show_listen);
    EXPECT_FALSE(got.show_processes);
    EXPECT_EQ(got.proc_limit, 17);
    EXPECT_FALSE(got.show_proc_threads);
    EXPECT_TRUE(got.show_proc_network);
    EXPECT_EQ(got.proc_sort, ProcSort::Time);

    EXPECT_EQ(got.start_view, View::Processes);

    EXPECT_FALSE(got.tui_use_unicode);
    EXPECT_FALSE(got.tui_use_colors);
    EXPECT_EQ(got.sparkline_length, 19);
    EXPECT_EQ(got.proc_sort_col, 2);

    EXPECT_EQ(got.excluded_interfaces,  cfg.excluded_interfaces);
    EXPECT_EQ(got.excluded_filesystems, cfg.excluded_filesystems);
    EXPECT_EQ(got.excluded_sensors,     cfg.excluded_sensors);

    fs::remove_all(dir);
}

// Each sort key must survive the writer/parser pair, not just the default.
TEST(ConfigSaveTest, EverySortOrderSurvivesRoundTrip) {
    fs::path dir = fs::temp_directory_path() /
                   ("sysmon_test_sort_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const std::string path = (dir / "sysmon.conf").string();

    for (const ProcSort sort : {ProcSort::Cpu, ProcSort::Memory, ProcSort::Pid,
                                ProcSort::Name, ProcSort::Time}) {
        Config cfg = Config::defaults();
        cfg.proc_sort = sort;
        cfg.save_to(path);
        EXPECT_EQ(Config::load_from(path).proc_sort, sort);
    }

    fs::remove_all(dir);
}

// Every detail level and every view must survive the writer/parser pair, not
// just the defaults — a name that only the writer knows is lost in silence.
TEST(ConfigSaveTest, EveryDetailLevelSurvivesRoundTrip) {
    fs::path dir = fs::temp_directory_path() /
                   ("sysmon_test_detail_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const std::string path = (dir / "sysmon.conf").string();

    for (const DetailLevel level : {DetailLevel::Compact, DetailLevel::Normal,
                                    DetailLevel::Detailed, DetailLevel::Full}) {
        Config cfg = Config::defaults();
        cfg.detail_level = level;
        cfg.save_to(path);
        const Config got = Config::load_from(path);
        EXPECT_EQ(got.detail_level, level) << Config::detail_name(level);
        EXPECT_EQ(got.compact_mode(), level == DetailLevel::Compact);
    }
    fs::remove_all(dir);
}

TEST(ConfigSaveTest, EveryStartViewSurvivesRoundTrip) {
    fs::path dir = fs::temp_directory_path() /
                   ("sysmon_test_view_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const std::string path = (dir / "sysmon.conf").string();

    for (const View view : {View::Overview, View::Cpu, View::Memory, View::Gpu,
                            View::Disk, View::Network, View::Connections,
                            View::Processes, View::Sensors}) {
        Config cfg = Config::defaults();
        cfg.start_view = view;
        cfg.save_to(path);
        EXPECT_EQ(Config::load_from(path).start_view, view) << Config::view_name(view);
    }
    fs::remove_all(dir);
}

// A config file written before detail_level existed carries only the boolean.
TEST(ConfigLoadTest, LegacyCompactModeStillSelectsTheCompactLevel) {
    const std::string path = make_temp_conf("[display]\ncompact_mode = true\n");
    const Config cfg = Config::load_from(path);
    EXPECT_EQ(cfg.detail_level, DetailLevel::Compact);
    EXPECT_TRUE(cfg.compact_mode());
}

TEST(ConfigLoadTest, DetailLevelWinsOverLegacyCompactMode) {
    // Both keys present: the newer spelling decides, so an edited old file
    // does not silently keep the dashboard collapsed.
    const std::string path = make_temp_conf(
        "[display]\ncompact_mode = true\ndetail_level = full\n");
    const Config cfg = Config::load_from(path);
    EXPECT_EQ(cfg.detail_level, DetailLevel::Full);
    EXPECT_FALSE(cfg.compact_mode());
}

TEST(ConfigParseTest, DetailAndViewNamesRoundTripThroughTheirParsers) {
    for (const DetailLevel level : {DetailLevel::Compact, DetailLevel::Normal,
                                    DetailLevel::Detailed, DetailLevel::Full}) {
        const auto parsed = Config::parse_detail(Config::detail_name(level));
        ASSERT_TRUE(parsed.has_value()) << Config::detail_name(level);
        EXPECT_EQ(*parsed, level);
    }
    for (const View view : {View::Overview, View::Cpu, View::Memory, View::Gpu,
                            View::Disk, View::Network, View::Connections,
                            View::Processes, View::Sensors}) {
        const auto parsed = Config::parse_view(Config::view_name(view));
        ASSERT_TRUE(parsed.has_value()) << Config::view_name(view);
        EXPECT_EQ(*parsed, view);
    }
    EXPECT_FALSE(Config::parse_detail("enormous").has_value());
    EXPECT_FALSE(Config::parse_view("kitchen-sink").has_value());
}
