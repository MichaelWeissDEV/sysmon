#include "sysmon/battery_monitor.hpp"
#include "sysmon/platform.hpp"
#include "sysmon/utils.hpp"

#include <algorithm>
#include <cmath>

#if defined(SYSMON_LINUX)
#  include <filesystem>
#endif

#if defined(SYSMON_MACOS)
#  include <CoreFoundation/CoreFoundation.h>
#  include <IOKit/IOKitLib.h>
#endif

#if defined(SYSMON_WINDOWS)
#  include <windows.h>
#endif

namespace {

#if defined(SYSMON_MACOS)

/// Read one integer property from an IORegistry entry.
std::optional<long long> io_number(io_registry_entry_t entry, CFStringRef key) {
    auto ref = static_cast<CFNumberRef>(
        IORegistryEntryCreateCFProperty(entry, key, kCFAllocatorDefault, 0));
    if (ref == nullptr) return std::nullopt;

    std::optional<long long> result;
    if (CFGetTypeID(ref) == CFNumberGetTypeID()) {
        long long value = 0;
        if (CFNumberGetValue(ref, kCFNumberLongLongType, &value)) result = value;
    }
    CFRelease(ref);
    return result;
}

/// Read one boolean property from an IORegistry entry.
std::optional<bool> io_boolean(io_registry_entry_t entry, CFStringRef key) {
    auto ref = IORegistryEntryCreateCFProperty(entry, key, kCFAllocatorDefault, 0);
    if (ref == nullptr) return std::nullopt;

    std::optional<bool> result;
    if (CFGetTypeID(ref) == CFBooleanGetTypeID()) {
        result = CFBooleanGetValue(static_cast<CFBooleanRef>(ref));
    }
    CFRelease(ref);
    return result;
}

#endif // SYSMON_MACOS

} // namespace

std::string BatteryMonitor::normalize_state(const std::string& raw) {
    const std::string lower = utils::to_lower(utils::trim(raw));
    if (lower.empty()) return "";
    if (lower == "charging")     return "Charging";
    if (lower == "discharging")  return "Discharging";
    if (lower == "full")         return "Full";
    if (lower == "not charging") return "Not charging";
    if (lower == "unknown")      return "Unknown";
    // Preserve anything else verbatim rather than dropping information.
    return utils::trim(raw);
}

BatteryStats BatteryMonitor::read() {
    BatteryStats stats;

#if defined(SYSMON_LINUX)
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base{"/sys/class/power_supply"};
    if (!fs::exists(base, ec)) return stats;

    for (const auto& entry : fs::directory_iterator(base, ec)) {
        if (ec) break;
        const std::string dir  = entry.path().string();
        const std::string type = utils::read_first_line(dir + "/type").value_or("");

        if (type == "Mains") {
            if (auto online = utils::read_first_line(dir + "/online")) {
                if (*online == "1") stats.ac_connected = true;
            }
            continue;
        }
        if (type != "Battery" || stats.present) continue;

        stats.present    = true;
        stats.state      = normalize_state(utils::read_first_line(dir + "/status").value_or(""));
        stats.technology = utils::read_first_line(dir + "/technology").value_or("");
        stats.vendor     = utils::read_first_line(dir + "/manufacturer").value_or("");

        if (auto capacity = utils::read_first_line(dir + "/capacity")) {
            stats.percent = utils::to_double(*capacity);
        }
        if (auto cycles = utils::read_first_line(dir + "/cycle_count")) {
            if (auto v = utils::to_int(*cycles)) {
                if (*v > 0) stats.cycle_count = static_cast<unsigned int>(*v);
            }
        }
        if (auto temp = utils::read_first_line(dir + "/temp")) {
            // Reported in tenths of a degree.
            if (auto v = utils::to_double(*temp)) stats.temperature_celsius = *v / 10.0;
        }
        if (auto voltage = utils::read_first_line(dir + "/voltage_now")) {
            if (auto v = utils::to_double(*voltage)) stats.voltage_volts = *v / 1e6;
        }
        if (auto power = utils::read_first_line(dir + "/power_now")) {
            if (auto v = utils::to_double(*power)) stats.power_watts = *v / 1e6;
        }

        // Capacity is exposed either in charge (µAh) or energy (µWh) units,
        // depending on the driver.  Health is the ratio of the two capacities
        // and is unit independent, so either flavour works.
        auto read_pair = [&dir](const char* full_key, const char* design_key)
            -> std::pair<std::optional<double>, std::optional<double>> {
            auto full   = utils::read_first_line(std::string(dir) + "/" + full_key);
            auto design = utils::read_first_line(std::string(dir) + "/" + design_key);
            return {full.has_value()   ? utils::to_double(*full)   : std::nullopt,
                    design.has_value() ? utils::to_double(*design) : std::nullopt};
        };

        auto [full_charge, design_charge] = read_pair("charge_full", "charge_full_design");
        if (!full_charge.has_value()) {
            std::tie(full_charge, design_charge) = read_pair("energy_full", "energy_full_design");
        }
        if (full_charge.has_value()) {
            stats.full_capacity_mah = static_cast<uint64_t>(*full_charge / 1000.0);
        }
        if (design_charge.has_value()) {
            stats.design_capacity_mah = static_cast<uint64_t>(*design_charge / 1000.0);
        }
        if (full_charge.has_value() && design_charge.has_value() && *design_charge > 0) {
            stats.health_percent = *full_charge / *design_charge * 100.0;
        }

        if (auto now_charge = utils::read_first_line(dir + "/charge_now")) {
            if (auto v = utils::to_double(*now_charge)) {
                stats.current_capacity_mah = static_cast<uint64_t>(*v / 1000.0);
            }
        }

        // Remaining runtime, where the driver reports instantaneous power draw.
        if (stats.power_watts.has_value() && *stats.power_watts > 0.0) {
            if (auto energy_now = utils::read_first_line(dir + "/energy_now")) {
                if (auto v = utils::to_double(*energy_now)) {
                    stats.time_remaining_minutes = (v / 1e6) / *stats.power_watts * 60.0;
                }
            }
        }
    }

#elif defined(SYSMON_MACOS)
    CFMutableDictionaryRef matching = IOServiceMatching("AppleSmartBattery");
    if (matching == nullptr) return stats;

    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, matching);
    if (service == IO_OBJECT_NULL) return stats;   // desktop Mac: no battery

    stats.present = true;

    if (auto external = io_boolean(service, CFSTR("ExternalConnected"))) {
        stats.ac_connected = *external;
    }
    const auto is_charging = io_boolean(service, CFSTR("IsCharging")).value_or(false);
    const auto fully_charged = io_boolean(service, CFSTR("FullyCharged")).value_or(false);
    stats.state = fully_charged ? "Full" : (is_charging ? "Charging" : "Discharging");

    // On current macOS, CurrentCapacity/MaxCapacity are a percentage pair
    // (n out of 100).  The real charge in mAh is only in the AppleRaw* keys,
    // so mixing the two would compute a health of about 1 %.
    const auto current     = io_number(service, CFSTR("CurrentCapacity"));
    const auto maximum     = io_number(service, CFSTR("MaxCapacity"));
    const auto raw_current = io_number(service, CFSTR("AppleRawCurrentCapacity"));
    const auto raw_max     = io_number(service, CFSTR("AppleRawMaxCapacity"));
    const auto design      = io_number(service, CFSTR("DesignCapacity"));

    if (current.has_value() && maximum.has_value() && *maximum > 0) {
        stats.percent = static_cast<double>(*current) / static_cast<double>(*maximum) * 100.0;
    }
    if (raw_current.has_value() && *raw_current > 0) {
        stats.current_capacity_mah = static_cast<uint64_t>(*raw_current);
    }
    if (raw_max.has_value() && *raw_max > 0) {
        stats.full_capacity_mah = static_cast<uint64_t>(*raw_max);
    }
    if (design.has_value() && *design > 0) {
        stats.design_capacity_mah = static_cast<uint64_t>(*design);
        // Health compares like with like: both are raw mAh capacities.
        if (raw_max.has_value() && *raw_max > 0) {
            stats.health_percent =
                static_cast<double>(*raw_max) / static_cast<double>(*design) * 100.0;
        }
    }
    if (auto cycles = io_number(service, CFSTR("CycleCount"))) {
        if (*cycles >= 0) stats.cycle_count = static_cast<unsigned int>(*cycles);
    }
    if (auto temp = io_number(service, CFSTR("Temperature"))) {
        // Tenths of a degree Celsius; reject implausible readings rather than
        // reporting a battery at 900 °C.
        const double celsius = static_cast<double>(*temp) / 100.0;
        if (celsius > -20.0 && celsius < 100.0) stats.temperature_celsius = celsius;
    }
    if (auto voltage = io_number(service, CFSTR("Voltage"))) {
        if (*voltage > 0) stats.voltage_volts = static_cast<double>(*voltage) / 1000.0;
    }
    if (auto amperage = io_number(service, CFSTR("Amperage"))) {
        if (stats.voltage_volts.has_value()) {
            stats.power_watts = static_cast<double>(*amperage) / 1000.0 * *stats.voltage_volts;
        }
    }
    // The controller reports whichever estimate applies to the current state.
    const auto to_empty = io_number(service, CFSTR("TimeRemaining"));
    if (to_empty.has_value() && *to_empty > 0 && *to_empty < 60 * 24) {
        stats.time_remaining_minutes = static_cast<double>(*to_empty);
    }

    IOObjectRelease(service);

#elif defined(SYSMON_WINDOWS)
    SYSTEM_POWER_STATUS power{};
    if (!GetSystemPowerStatus(&power)) return stats;

    stats.ac_connected = (power.ACLineStatus == 1);

    // BATTERY_FLAG_NO_BATTERY
    if ((power.BatteryFlag & 128) != 0 || power.BatteryFlag == 255) {
        return stats;   // no battery installed, or state unknown
    }

    stats.present = true;
    if (power.BatteryLifePercent != 255) {
        stats.percent = static_cast<double>(power.BatteryLifePercent);
    }
    if (power.BatteryLifeTime != 0xFFFFFFFF) {
        stats.time_remaining_minutes = static_cast<double>(power.BatteryLifeTime) / 60.0;
    }

    if ((power.BatteryFlag & 8) != 0)         stats.state = "Charging";
    else if (stats.ac_connected)              stats.state = "Full";
    else                                      stats.state = "Discharging";
#endif

    if (stats.percent.has_value()) {
        stats.percent = std::max(0.0, std::min(100.0, *stats.percent));
    }
    return stats;
}
