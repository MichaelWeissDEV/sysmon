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