#include <gtest/gtest.h>
#include "sysmon/memory_monitor.hpp"

TEST(MemoryMonitorTest, ParseMeminfoValue) {
    const std::string data =
        "MemTotal:        8123456 kB\n"
        "MemAvailable:    6543210 kB\n"
        "SwapTotal:       2097152 kB\n"
        "SwapFree:        1048576 kB\n";

    EXPECT_EQ(MemoryMonitor::parse_meminfo_value("MemTotal", data).value_or(0),
              static_cast<uint64_t>(8123456) * 1024);
    EXPECT_EQ(MemoryMonitor::parse_meminfo_value("SwapFree", data).value_or(0),
              static_cast<uint64_t>(1048576) * 1024);
    EXPECT_FALSE(MemoryMonitor::parse_meminfo_value("MissingKey", data).has_value());
}

TEST(MemoryMonitorTest, ParseMeminfoValueMalformed) {
    const std::string data = "MemTotal: not-a-number kB\n";
    EXPECT_FALSE(MemoryMonitor::parse_meminfo_value("MemTotal", data).has_value());
}

TEST(MemoryMonitorTest, ReportsTotalRam) {
    MemoryMonitor mon;
    auto stats = mon.read();
    EXPECT_GT(stats.ram_total_bytes, 0ull);
}

TEST(MemoryMonitorTest, UsagePercentInRange) {
    MemoryMonitor mon;
    auto stats = mon.read();
    EXPECT_GE(stats.ram_usage_percent, 0.0);
    EXPECT_LE(stats.ram_usage_percent, 100.0);
}

TEST(MemoryMonitorTest, UsedNotExceedingTotal) {
    MemoryMonitor mon;
    auto stats = mon.read();
    if (stats.ram_total_bytes > 0) {
        EXPECT_LE(stats.ram_used_bytes, stats.ram_total_bytes);
    }
}