#include <gtest/gtest.h>
#include "sysmon/disk_monitor.hpp"

TEST(DiskMonitorTest, ReturnsAtLeastRoot) {
    DiskMonitor mon;
    auto disks = mon.read();
    EXPECT_FALSE(disks.empty()) << "Should find at least one mounted filesystem";
}

TEST(DiskMonitorTest, AllDisksHavePositiveTotal) {
    DiskMonitor mon;
    auto disks = mon.read();
    for (const auto& d : disks) {
        EXPECT_GT(d.total_bytes, 0u) << "Disk " << d.mountpoint << " has zero total bytes";
    }
}

TEST(DiskMonitorTest, UsagePercentInRange) {
    DiskMonitor mon;
    auto disks = mon.read();
    for (const auto& d : disks) {
        EXPECT_GE(d.usage_percent, 0.0);
        EXPECT_LE(d.usage_percent, 100.0);
    }
}
