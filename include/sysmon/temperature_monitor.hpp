/**
 * @file temperature_monitor.hpp
 * @brief CPU and system temperature sensor reader.
 */

#ifndef SYSMON_TEMPERATURE_MONITOR_HPP
#define SYSMON_TEMPERATURE_MONITOR_HPP

#include "sysmon/stats.hpp"
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Reads temperature sensors.
 *
 * Linux: /sys/class/hwmon/ and /sys/class/thermal/
 * macOS: IOKit SMC sensors.
 */
class TemperatureMonitor {
public:
    /**
     * @brief Read all available temperature sensors.
     */
    TemperatureStats read();

    /**
     * @brief Convenience: return CPU package temperature only.
     */
    std::optional<double> read_cpu_temperature();

private:
    std::optional<double> read_temperature_from_hwmon(const std::string& hwmon_path);
    std::optional<double> read_temperature_from_thermal(const std::string& thermal_path);
    TemperatureStats      read_all_sensors_linux();
    TemperatureStats      read_all_sensors_macos();
};

#endif // SYSMON_TEMPERATURE_MONITOR_HPP