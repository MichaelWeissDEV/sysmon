/**
 * @file battery_monitor.hpp
 * @brief Battery charge, health and power-source monitor.
 */

#ifndef SYSMON_BATTERY_MONITOR_HPP
#define SYSMON_BATTERY_MONITOR_HPP

#include "sysmon/stats.hpp"
#include <string>

/**
 * @brief Reads battery state and the current power source.
 *
 * - Linux:   /sys/class/power_supply/BAT*
 * - macOS:   IOKit AppleSmartBattery
 * - Windows: GetSystemPowerStatus
 *
 * On a desktop without a battery, `present` stays false and every optional
 * field stays empty; the renderers then omit the section entirely.
 */
class BatteryMonitor {
public:
    /** @brief Read current battery and power-source state. */
    BatteryStats read();

    /**
     * @brief Normalise the many spellings of a charging state.
     *
     * Linux writes "Charging"/"Discharging"/"Full"/"Not charging"; macOS and
     * Windows report booleans instead.  Exposed for testing.
     */
    static std::string normalize_state(const std::string& raw);
};

#endif // SYSMON_BATTERY_MONITOR_HPP
