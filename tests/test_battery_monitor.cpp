#include <gtest/gtest.h>

#include "sysmon/battery_monitor.hpp"

TEST(BatteryMonitorTest, NormalizesKnownStateSpellings) {
    EXPECT_EQ(BatteryMonitor::normalize_state("Charging"), "Charging");
    EXPECT_EQ(BatteryMonitor::normalize_state("discharging"), "Discharging");
    EXPECT_EQ(BatteryMonitor::normalize_state("  Full  "), "Full");
    EXPECT_EQ(BatteryMonitor::normalize_state("Not charging"), "Not charging");
    EXPECT_TRUE(BatteryMonitor::normalize_state("").empty());
}

TEST(BatteryMonitorTest, UnknownStatesArePreservedNotDiscarded) {
    EXPECT_EQ(BatteryMonitor::normalize_state("Calibrating"), "Calibrating");
}

TEST(BatteryMonitorTest, ReadNeverReportsImpossibleValues) {
    BatteryMonitor monitor;
    const BatteryStats stats = monitor.read();

    if (!stats.present) {
        // A machine without a battery must not invent one.
        EXPECT_FALSE(stats.percent.has_value());
        EXPECT_FALSE(stats.cycle_count.has_value());
        return;
    }

    if (stats.percent.has_value()) {
        EXPECT_GE(stats.percent.value(), 0.0);
        EXPECT_LE(stats.percent.value(), 100.0);
    }
    if (stats.health_percent.has_value()) {
        // Health is full capacity over design capacity; a value near zero means
        // the two were read in different units.
        EXPECT_GT(stats.health_percent.value(), 5.0);
        EXPECT_LE(stats.health_percent.value(), 200.0);
    }
    if (stats.temperature_celsius.has_value()) {
        EXPECT_GT(stats.temperature_celsius.value(), -30.0);
        EXPECT_LT(stats.temperature_celsius.value(), 100.0);
    }
}
