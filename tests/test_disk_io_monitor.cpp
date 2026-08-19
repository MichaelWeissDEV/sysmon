#include <gtest/gtest.h>
#include "sysmon/disk_io_monitor.hpp"

TEST(DiskIOMonitorTest, TwoSamplesProduceNoNegativeRates) {
    DiskIOMonitor mon;
    mon.read();  // warm-up sample initializes counters
    auto second = mon.read();
    for (const auto& d : second) {
        EXPECT_GE(d.read_bytes_per_sec, 0.0);
        EXPECT_GE(d.write_bytes_per_sec, 0.0);
        EXPECT_GE(d.read_ops_per_sec, 0.0);
        EXPECT_GE(d.write_ops_per_sec, 0.0);
    }
}

TEST(DiskIOMonitorTest, FirstSampleHasZeroRates) {
    DiskIOMonitor mon;
    auto first = mon.read();
    for (const auto& d : first) {
        EXPECT_DOUBLE_EQ(d.read_bytes_per_sec, 0.0);
        EXPECT_DOUBLE_EQ(d.write_bytes_per_sec, 0.0);
    }
}