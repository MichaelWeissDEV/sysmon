#include <gtest/gtest.h>
#include "sysmon/cpu_monitor.hpp"

using CpuTimes = CpuMonitor::CpuTimes;

// ---------------------------------------------------------------------------
// Pure unit tests for delta computation.
// ---------------------------------------------------------------------------

TEST(CpuUsageDeltaTest, ZeroDelta) {
    CpuTimes a{}, b{};
    EXPECT_DOUBLE_EQ(CpuMonitor::usage_from_delta(a, b), 0.0);
}

TEST(CpuUsageDeltaTest, IdleOnlyYieldsZero) {
    CpuTimes a; a.idle = 1000;
    CpuTimes b; b.idle = 2000;
    double user = -1, sys = -1, io = -1, idle = -1;
    double usage = CpuMonitor::usage_from_delta(a, b, &user, &sys, &io, &idle);
    EXPECT_DOUBLE_EQ(usage, 0.0);
    EXPECT_DOUBLE_EQ(user, 0.0);
    EXPECT_DOUBLE_EQ(sys, 0.0);
    EXPECT_DOUBLE_EQ(io, 0.0);
    EXPECT_DOUBLE_EQ(idle, 100.0);
}

TEST(CpuUsageDeltaTest, FullyBusyYieldsHundred) {
    CpuTimes a; a.user = 100;
    CpuTimes b; b.user = 200;
    double usage = CpuMonitor::usage_from_delta(a, b);
    EXPECT_DOUBLE_EQ(usage, 100.0);
}

TEST(CpuUsageDeltaTest, HalfBusy) {
    CpuTimes a; a.user = 100; a.idle = 100;
    CpuTimes b; b.user = 150; b.idle = 150;
    double usage = CpuMonitor::usage_from_delta(a, b);
    EXPECT_NEAR(usage, 50.0, 0.0001);
}

TEST(CpuUsageDeltaTest, CounterResetYieldsZeroNotHugeValue) {
    // Simulate a counter reset: total decreases between samples.
    CpuTimes a; a.user = 500; a.idle = 500;
    CpuTimes b; b.user = 10;  b.idle = 10;
    double user = -1, sys = -1, io = -1, idle = -1;
    double usage = CpuMonitor::usage_from_delta(a, b, &user, &sys, &io, &idle);
    EXPECT_DOUBLE_EQ(usage, 0.0);
    EXPECT_DOUBLE_EQ(idle, 100.0);
}

TEST(CpuUsageDeltaTest, ComponentPercentagesClamped) {
    // idle delta negative (single counter wrapped while total did not): the
    // usage must still be clamped to [0, 100].
    CpuTimes a; a.user = 100; a.idle = 100;
    CpuTimes b; b.user = 300; b.idle = 50;   // idle went *down*
    double usage = CpuMonitor::usage_from_delta(a, b);
    EXPECT_GE(usage, 0.0);
    EXPECT_LE(usage, 100.0);
}

// ---------------------------------------------------------------------------
// Platform integration tests.
// ---------------------------------------------------------------------------

TEST(CpuMonitorTest, TwoSamplesWithinRange) {
    CpuMonitor mon;
    mon.read();
    auto second = mon.read();
    EXPECT_GE(second.usage_percent, 0.0);
    EXPECT_LE(second.usage_percent, 100.0);
    for (const auto& c : second.per_core) {
        EXPECT_GE(c.usage_percent, 0.0);
        EXPECT_LE(c.usage_percent, 100.0);
    }
}

TEST(CpuMonitorTest, FirstSampleIsZeroUsage) {
    CpuMonitor mon;
    auto stats = mon.read();
    EXPECT_DOUBLE_EQ(stats.usage_percent, 0.0);
}

TEST(CpuMonitorTest, ReportsCoreCounts) {
    CpuMonitor mon;
    auto stats = mon.read();
    // On both CI platforms logical and physical cores are > 0.
    EXPECT_GT(stats.logical_cores, 0u);
    EXPECT_GT(stats.physical_cores, 0u);
    EXPECT_FALSE(stats.model.empty());
}

TEST(CpuMonitorTest, UsageComponentsWithinRange) {
    CpuMonitor mon;
    mon.read();
    auto stats = mon.read();
    for (double v : {stats.user_percent, stats.system_percent,
                     stats.iowait_percent, stats.idle_percent}) {
        EXPECT_GE(v, 0.0);
        EXPECT_LE(v, 100.0);
    }
}