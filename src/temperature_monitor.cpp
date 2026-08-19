#include "sysmon/temperature_monitor.hpp"
#include "sysmon/utils.hpp"
#include "sysmon/platform.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <climits>

#if defined(SYSMON_MACOS)
// macOS: we use a simple smc-read approach via IOKit if available.
// For simplicity, we fall back gracefully if IOKit is not linked.
#endif

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::optional<double> TemperatureMonitor::read_cpu_temperature() {
    auto stats = read();
    return stats.cpu_package;
}

TemperatureStats TemperatureMonitor::read() {
#if defined(SYSMON_LINUX)
    return read_all_sensors_linux();
#elif defined(SYSMON_MACOS)
    return read_all_sensors_macos();
#else
    return {};
#endif
}

// ---------------------------------------------------------------------------
// Linux
// ---------------------------------------------------------------------------

#if defined(SYSMON_LINUX)

TemperatureStats TemperatureMonitor::read_all_sensors_linux() {
    TemperatureStats result;

    namespace fs = std::filesystem;
    const std::string hwmon_base = "/sys/class/hwmon/";

    try {
        if (!fs::exists(hwmon_base)) goto try_thermal;

        for (const auto& hwmon_entry : fs::directory_iterator(hwmon_base)) {
            if (!hwmon_entry.is_directory() && !hwmon_entry.is_symlink()) continue;

            std::string hwmon_dir = hwmon_entry.path().string();

            // Read the device name (e.g. "coretemp", "k10temp", "nct6775")
            auto name_content = utils::read_file(hwmon_dir + "/name");
            std::string hwmon_name = name_content.has_value()
                                     ? utils::trim(name_content.value()) : "";

            // Iterate temp*_input files
            try {
                for (const auto& file_entry : fs::directory_iterator(hwmon_dir)) {
                    if (!file_entry.is_regular_file()) continue;
                    std::string fname = file_entry.path().filename().string();

                    // Match temp<N>_input
                    if (fname.find("temp") == 0 && fname.find("_input") != std::string::npos) {
                        auto val = utils::read_file(file_entry.path().string());
                        if (!val.has_value()) continue;

                        double temp_c;
                        try { temp_c = std::stod(val.value()) / 1000.0; }
                        catch (...) { continue; }

                        // Get label (temp<N>_label)
                        std::string base = fname.substr(0, fname.find("_input"));
                        auto label_val = utils::read_file(hwmon_dir + "/" + base + "_label");
                        std::string label = label_val.has_value()
                                            ? utils::trim(label_val.value()) : base;
                        if (!hwmon_name.empty()) label = hwmon_name + ": " + label;

                        // Get thresholds
                        std::optional<double> high, crit;
                        auto h = utils::read_file(hwmon_dir + "/" + base + "_max");
                        if (h.has_value()) try { high = std::stod(h.value()) / 1000.0; } catch (...) {}
                        auto c = utils::read_file(hwmon_dir + "/" + base + "_crit");
                        if (c.has_value()) try { crit = std::stod(c.value()) / 1000.0; } catch (...) {}

                        SensorReading reading;
                        reading.name               = label;
                        reading.temperature_celsius = temp_c;
                        reading.high               = high;
                        reading.critical           = crit;
                        result.sensors.push_back(reading);

                        // Detect CPU package temperature
                        std::string lower_name = hwmon_name;
                        std::transform(lower_name.begin(), lower_name.end(),
                                       lower_name.begin(), ::tolower);
                        std::string lower_label = label;
                        std::transform(lower_label.begin(), lower_label.end(),
                                       lower_label.begin(), ::tolower);

                        if ((lower_name.find("coretemp") != std::string::npos ||
                             lower_name.find("k10temp")  != std::string::npos ||
                             lower_name.find("zenpower") != std::string::npos) &&
                            (lower_label.find("package") != std::string::npos ||
                             lower_label.find("tdie")    != std::string::npos ||
                             lower_label.find("tccd")    == std::string::npos)) {
                            if (!result.cpu_package.has_value() ||
                                temp_c > result.cpu_package.value()) {
                                result.cpu_package = temp_c;
                            }
                        }
                    }
                }
            } catch (...) {}
        }
    } catch (...) {}

try_thermal:
    // Fallback: /sys/class/thermal/thermal_zone*/temp
    try {
        const std::string thermal_base = "/sys/class/thermal/";
        if (fs::exists(thermal_base)) {
            for (const auto& entry : fs::directory_iterator(thermal_base)) {
                std::string dir = entry.path().string();
                if (dir.find("thermal_zone") == std::string::npos) continue;

                auto temp_val = utils::read_file(dir + "/temp");
                if (!temp_val.has_value()) continue;

                double temp_c;
                try { temp_c = std::stod(temp_val.value()) / 1000.0; }
                catch (...) { continue; }

                auto type_val = utils::read_file(dir + "/type");
                std::string label = type_val.has_value()
                                    ? utils::trim(type_val.value()) : "zone";

                SensorReading r;
                r.name               = "thermal: " + label;
                r.temperature_celsius = temp_c;
                result.sensors.push_back(r);

                if (!result.cpu_package.has_value()) {
                    result.cpu_package = temp_c;
                }
            }
        }
    } catch (...) {}

    return result;
}

#else
TemperatureStats TemperatureMonitor::read_all_sensors_linux() { return {}; }
#endif

// ---------------------------------------------------------------------------
// macOS (stub — real SMC reading requires IOKit framework linkage)
// ---------------------------------------------------------------------------

#if defined(SYSMON_MACOS)
#  include <CoreFoundation/CoreFoundation.h>
#  include <IOKit/IOKitLib.h>
#  include <IOKit/ps/IOPowerSources.h>
#  include <IOKit/ps/IOPSKeys.h>
#  include <sys/sysctl.h>

TemperatureStats TemperatureMonitor::read_all_sensors_macos() {
    TemperatureStats result;

    // 1. Read Battery Temperature from AppleSmartBattery via IOKit
    CFMutableDictionaryRef matching = IOServiceMatching("AppleSmartBattery");
    if (matching) {
        io_service_t battery = IOServiceGetMatchingService(kIOMainPortDefault, matching);
        if (battery != IO_OBJECT_NULL) {
            CFNumberRef temp_num = (CFNumberRef)IORegistryEntryCreateCFProperty(
                battery, CFSTR("Temperature"), kCFAllocatorDefault, 0);
            if (temp_num && CFGetTypeID(temp_num) == CFNumberGetTypeID()) {
                long long temp_val = 0;
                CFNumberGetValue(temp_num, kCFNumberLongLongType, &temp_val);
                // AppleSmartBattery Temperature is in tenths of a degree Celsius (e.g. 301 = 30.1 °C)
                double temp_c = static_cast<double>(temp_val) / 10.0;
                if (temp_c > 0.0 && temp_c < 120.0) {
                    SensorReading r;
                    r.name = "Battery Temperature";
                    r.chip = "AppleSmartBattery";
                    r.temperature_celsius = temp_c;
                    r.high = 45.0;
                    r.critical = 60.0;
                    result.sensors.push_back(r);
                }
                CFRelease(temp_num);
            }
            IOObjectRelease(battery);
        }
    }

    // 2. Read Thermal Level from sysctl
    int thermal_level = 0;
    size_t tlen = sizeof(thermal_level);
    if (sysctlbyname("machdep.xcpm.cpu_thermal_level", &thermal_level, &tlen, nullptr, 0) == 0 ||
        sysctlbyname("hw.thermal.level", &thermal_level, &tlen, nullptr, 0) == 0) {
        // Map thermal level index to estimated SOC temperature equivalent
        // Level 0: Nominal (~45-55°C), Level 1: Moderate (~65°C), Level 2: Heavy (~80°C), Level 3: Trapping (~95°C)
        double estimated_c = 45.0 + (thermal_level * 15.0);
        SensorReading r;
        r.name = "SOC Thermal Pressure";
        r.chip = "Apple Silicon PMU";
        r.temperature_celsius = estimated_c;
        r.high = 80.0;
        r.critical = 95.0;
        result.sensors.push_back(r);
        result.cpu_package = estimated_c;
    }

    return result;
}
#else
TemperatureStats TemperatureMonitor::read_all_sensors_macos() { return {}; }
#endif

// ---------------------------------------------------------------------------
// Legacy compatibility wrappers (used by older code paths)
// ---------------------------------------------------------------------------

std::optional<double> TemperatureMonitor::read_temperature_from_hwmon(const std::string& hwmon_path) {
    (void)hwmon_path;
    return read_cpu_temperature();
}

std::optional<double> TemperatureMonitor::read_temperature_from_thermal(const std::string& thermal_path) {
    (void)thermal_path;
    return std::nullopt;
}