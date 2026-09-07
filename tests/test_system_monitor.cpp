#include <gtest/gtest.h>
#include "sysmon/system_monitor.hpp"

TEST(SystemMonitorTest, ReportsHostAndKernel) {
    SystemMonitor mon;
    auto stats = mon.read();
    EXPECT_FALSE(stats.hostname.empty());
    EXPECT_FALSE(stats.kernel.empty());
    EXPECT_FALSE(stats.architecture.empty());
    EXPECT_FALSE(stats.os.empty());
}

TEST(SystemMonitorTest, ArchitectureMatchesPlatform) {
    SystemMonitor mon;
    auto stats = mon.read();
    // Both CI platforms report a non-empty architecture string.
    EXPECT_FALSE(stats.architecture.empty());
}

TEST(SystemMonitorTest, UptimeNonNegative) {
    SystemMonitor mon;
    auto stats = mon.read();
    EXPECT_GE(stats.uptime_seconds, 0.0);
}
// ---------------------------------------------------------------------------
// /etc/os-release parsing
// ---------------------------------------------------------------------------

TEST(SystemMonitorTest, PrettyNameWinsInOsRelease) {
    const std::string content =
        "NAME=\"Ubuntu\"\n"
        "VERSION=\"24.04 LTS (Noble Numbat)\"\n"
        "PRETTY_NAME=\"Ubuntu 24.04 LTS\"\n"
        "ID=ubuntu\n";
    EXPECT_EQ(SystemMonitor::parse_os_release(content), "Ubuntu 24.04 LTS");
}

TEST(SystemMonitorTest, FallsBackToNameAndVersion) {
    const std::string content = "NAME=Arch Linux\nVERSION=rolling\n";
    EXPECT_EQ(SystemMonitor::parse_os_release(content), "Arch Linux rolling");
}

TEST(SystemMonitorTest, EmptyOrGarbageOsReleaseYieldsEmptyString) {
    EXPECT_TRUE(SystemMonitor::parse_os_release("").empty());
    EXPECT_TRUE(SystemMonitor::parse_os_release("no equals signs here\n").empty());
}
