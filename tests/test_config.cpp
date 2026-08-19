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
    cfg.compact_mode = true;
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