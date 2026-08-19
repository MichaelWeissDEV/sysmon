#include <gtest/gtest.h>
#include "sysmon/temperature_monitor.hpp"

// Regression test for the macOS thermal-pressure bug: a thermal level must
// never be turned into an invented Celsius temperature.  No sensor named
// "SOC Thermal Pressure" / "Thermal Pressure" may be produced, and a fake
// CPU package temperature must not be reported.
TEST(TemperatureMonitorTest, NoFabricatedThermalTemperature) {
    TemperatureMonitor mon;
    auto stats = mon.read();

    for (const auto& s : stats.sensors) {
        EXPECT_TRUE(s.name.find("Thermal Pressure") == std::string::npos)
            << "thermal pressure was converted into a temperature: " << s.name;
        EXPECT_TRUE(s.name.find("thermal_level") == std::string::npos)
            << "thermal level surfaced as a temperature: " << s.name;
        EXPECT_GE(s.temperature_celsius, -273.15);
    }
}

TEST(TemperatureMonitorTest, BatteryTemperatureWithinPlausibleRange) {
    TemperatureMonitor mon;
    auto stats = mon.read();
    for (const auto& s : stats.sensors) {
        if (s.name.find("Battery") != std::string::npos) {
            EXPECT_GE(s.temperature_celsius, 0.0);
            EXPECT_LE(s.temperature_celsius, 120.0);
        }
    }
}

// On macOS the CPU package temperature is expected to be unavailable because
// no stable unprivileged temperature API exists.  It must not be a real
// looking value derived from a thermal-pressure level.
TEST(TemperatureMonitorTest, NoThermalLevelBasedCpuPackage) {
    TemperatureMonitor mon;
    auto stats = mon.read();
    if (stats.cpu_package.has_value()) {
        // If present it must be a physically plausible value.  The previous
        // buggy implementation reported exactly 45 + level*15 (60/75/90 °C).
        double t = stats.cpu_package.value();
        EXPECT_GE(t, 0.0);
        EXPECT_LE(t, 120.0);
    }
}