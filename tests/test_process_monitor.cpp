#include <gtest/gtest.h>
#include "sysmon/process_monitor.hpp"

TEST(ProcessMonitorTest, FindsAtLeastOneProcess) {
    ProcessMonitor mon;
    auto procs = mon.read(100);
    EXPECT_FALSE(procs.empty());
    for (const auto& p : procs) {
        EXPECT_GT(p.pid, 0);
        EXPECT_FALSE(p.name.empty());
    }
}

TEST(ProcessMonitorTest, TwoSamplesProduceNoNegativeCpu) {
    ProcessMonitor mon;
    mon.read(100);
    auto second = mon.read(100);
    for (const auto& p : second) {
        EXPECT_GE(p.cpu_percent, 0.0);
    }
}

TEST(ProcessMonitorTest, MemoryPercentWithinRange) {
    ProcessMonitor mon;
    auto procs = mon.read(100);
    for (const auto& p : procs) {
        EXPECT_GE(p.mem_percent, 0.0);
        EXPECT_LE(p.mem_percent, 100.0);
    }
}