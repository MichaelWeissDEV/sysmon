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
 * @brief Reads temperature and fan sensors.
 *
 * - Linux:   /sys/class/hwmon/ and /sys/class/thermal/, including fan tachometers
 * - macOS:   IOKit (battery thermistor); die temperatures are not exposed to
 *            unprivileged processes on Apple Silicon and are reported as N/A
 * - Windows: no unprivileged, universally supported sensor API exists, so no
 *            readings are reported rather than guessed ones
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

    /// Fill hottest_celsius / hottest_name from the collected sensor list.
    static void summarize(TemperatureStats& stats);
};

#endif // SYSMON_TEMPERATURE_MONITOR_HPP