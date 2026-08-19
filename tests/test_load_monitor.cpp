#include <gtest/gtest.h>
#include "sysmon/load_monitor.hpp"

TEST(LoadMonitorTest, ReportsLoadAverages) {
    LoadMonitor mon;
    auto stats = mon.read();
    // Load averages should be present (>= 0) on both CI platforms.
    EXPECT_GE(stats.load_1min, 0.0);
    EXPECT_GE(stats.load_5min, 0.0);
    EXPECT_GE(stats.load_15min, 0.0);
}

TEST(LoadMonitorTest, ReportsProcessCount) {
    LoadMonitor mon;
    auto stats = mon.read();
    // At minimum the running process itself is present.
    EXPECT_GT(stats.total_processes, 0u);
}