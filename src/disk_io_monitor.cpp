#include "sysmon/disk_io_monitor.hpp"
#include "sysmon/utils.hpp"
#include "sysmon/platform.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>
#include <filesystem>

#if defined(SYSMON_MACOS)
#  include <CoreFoundation/CoreFoundation.h>
#  include <IOKit/IOKitLib.h>
#  include <IOKit/storage/IOBlockStorageDriver.h>
#  include <IOKit/storage/IOMedia.h>
#  include <IOKit/IOBSD.h>
#endif

DiskIOMonitor::DiskIOMonitor() = default;

std::vector<DiskIOStats> DiskIOMonitor::read() {
#if defined(SYSMON_LINUX)
    return read_linux();
#elif defined(SYSMON_MACOS)
    return read_macos();
#else
    return {};
#endif
}

// ---------------------------------------------------------------------------
// Linux: /proc/diskstats
// ---------------------------------------------------------------------------

#if defined(SYSMON_LINUX)

std::vector<DiskIOStats> DiskIOMonitor::read_linux() {
    auto diskstats = utils::read_file("/proc/diskstats");
    if (!diskstats.has_value()) return {};

    auto now = std::chrono::steady_clock::now();
    std::vector<DiskIOStats> result;
    std::istringstream iss(diskstats.value());
    std::string line;

    while (std::getline(iss, line)) {
        auto parts = utils::split(line, ' ');
        parts.erase(std::remove_if(parts.begin(), parts.end(),
                    [](const std::string& s) { return s.empty(); }), parts.end());

        if (parts.size() < 10) continue;

        std::string dev = parts[2];
        if (!is_physical_device(dev)) continue;

        uint64_t reads_completed  = 0;
        uint64_t sectors_read     = 0;
        uint64_t writes_completed = 0;
        uint64_t sectors_written  = 0;

        try {
            reads_completed  = std::stoull(parts[3]);
            sectors_read     = std::stoull(parts[5]);
            writes_completed = std::stoull(parts[7]);
            sectors_written  = std::stoull(parts[9]);
        } catch (...) { continue; }

        constexpr uint64_t sector_size = 512;
        uint64_t read_bytes    = sectors_read    * sector_size;
        uint64_t written_bytes = sectors_written * sector_size;

        DiskIOStats ios;
        ios.device = dev;

        auto it = previous_.find(dev);
        if (it != previous_.end()) {
            double dt = std::chrono::duration<double>(now - it->second.timestamp).count();
            if (dt > 0.0) {
                ios.read_bytes_per_sec  = static_cast<double>(read_bytes    - it->second.read_bytes)  / dt;
                ios.write_bytes_per_sec = static_cast<double>(written_bytes - it->second.write_bytes) / dt;
                ios.read_ops_per_sec    = static_cast<double>(reads_completed  - it->second.read_ios)  / dt;
                ios.write_ops_per_sec   = static_cast<double>(writes_completed - it->second.write_ios) / dt;
                if (ios.read_bytes_per_sec  < 0) ios.read_bytes_per_sec  = 0;
                if (ios.write_bytes_per_sec < 0) ios.write_bytes_per_sec = 0;
                if (ios.read_ops_per_sec    < 0) ios.read_ops_per_sec    = 0;
                if (ios.write_ops_per_sec   < 0) ios.write_ops_per_sec   = 0;
            }
        }

        DeviceSnapshot snap;
        snap.read_bytes  = read_bytes;
        snap.write_bytes = written_bytes;
        snap.read_ios    = reads_completed;
        snap.write_ios   = writes_completed;
        snap.timestamp   = now;
        previous_[dev]   = snap;

        result.push_back(ios);
    }

    std::sort(result.begin(), result.end(), [](const DiskIOStats& a, const DiskIOStats& b) {
        return a.device < b.device;
    });

    return result;
}

bool DiskIOMonitor::is_physical_device(const std::string& dev) {
    if (dev.empty()) return false;
    namespace fs = std::filesystem;
    std::string sysfs_path = "/sys/block/" + dev;
    return fs::exists(sysfs_path);
}

#else
std::vector<DiskIOStats> DiskIOMonitor::read_linux() { return {}; }
bool DiskIOMonitor::is_physical_device(const std::string&) { return false; }
#endif

// ---------------------------------------------------------------------------
// macOS: IOKit IOBlockStorageDriver statistics
// ---------------------------------------------------------------------------

#if defined(SYSMON_MACOS)

std::vector<DiskIOStats> DiskIOMonitor::read_macos() {
    std::vector<DiskIOStats> result;
    auto now = std::chrono::steady_clock::now();

    CFMutableDictionaryRef matching = IOServiceMatching(kIOBlockStorageDriverClass);
    if (!matching) return result;

    io_iterator_t iterator = IO_OBJECT_NULL;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator);
    if (kr != KERN_SUCCESS || iterator == IO_OBJECT_NULL) return result;

    io_registry_entry_t drive = IO_OBJECT_NULL;
    while ((drive = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        // Look for BSD name
        io_registry_entry_t child = IO_OBJECT_NULL;
        std::string bsd_name;
        if (IORegistryEntryGetChildEntry(drive, kIOServicePlane, &child) == KERN_SUCCESS) {
            CFStringRef bsd_str = (CFStringRef)IORegistryEntryCreateCFProperty(
                child, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
            if (bsd_str) {
                char buf[64] = {0};
                if (CFStringGetCString(bsd_str, buf, sizeof(buf), kCFStringEncodingUTF8)) {
                    bsd_name = buf;
                }
                CFRelease(bsd_str);
            }
            IOObjectRelease(child);
        }

        if (bsd_name.empty()) {
            // Fallback: check device name on driver itself
            io_name_t name;
            if (IORegistryEntryGetName(drive, name) == KERN_SUCCESS) {
                bsd_name = name;
            } else {
                bsd_name = "disk";
            }
        }

        // Get Statistics dictionary
        CFDictionaryRef props = nullptr;
        if (IORegistryEntryCreateCFProperties(drive, (CFMutableDictionaryRef*)&props, kCFAllocatorDefault, 0) == KERN_SUCCESS && props) {
            CFDictionaryRef stats_dict = (CFDictionaryRef)CFDictionaryGetValue(props, CFSTR(kIOBlockStorageDriverStatisticsKey));
            if (stats_dict && CFGetTypeID(stats_dict) == CFDictionaryGetTypeID()) {
                uint64_t read_bytes = 0, write_bytes = 0;
                uint64_t read_ops = 0, write_ops = 0;

                auto get_num = [&](CFStringRef key) -> uint64_t {
                    CFNumberRef num = (CFNumberRef)CFDictionaryGetValue(stats_dict, key);
                    if (num && CFGetTypeID(num) == CFNumberGetTypeID()) {
                        long long val = 0;
                        CFNumberGetValue(num, kCFNumberLongLongType, &val);
                        return val > 0 ? static_cast<uint64_t>(val) : 0;
                    }
                    return 0;
                };

                read_bytes  = get_num(CFSTR("Bytes (Read)"));
                write_bytes = get_num(CFSTR("Bytes (Write)"));
                read_ops    = get_num(CFSTR("Operations (Read)"));
                write_ops   = get_num(CFSTR("Operations (Write)"));

                DiskIOStats ios;
                ios.device = bsd_name;

                auto it = previous_.find(bsd_name);
                if (it != previous_.end()) {
                    double dt = std::chrono::duration<double>(now - it->second.timestamp).count();
                    if (dt > 0.0) {
                        ios.read_bytes_per_sec  = static_cast<double>(read_bytes - it->second.read_bytes) / dt;
                        ios.write_bytes_per_sec = static_cast<double>(write_bytes - it->second.write_bytes) / dt;
                        ios.read_ops_per_sec    = static_cast<double>(read_ops - it->second.read_ios) / dt;
                        ios.write_ops_per_sec   = static_cast<double>(write_ops - it->second.write_ios) / dt;
                        if (ios.read_bytes_per_sec < 0) ios.read_bytes_per_sec = 0;
                        if (ios.write_bytes_per_sec < 0) ios.write_bytes_per_sec = 0;
                        if (ios.read_ops_per_sec < 0) ios.read_ops_per_sec = 0;
                        if (ios.write_ops_per_sec < 0) ios.write_ops_per_sec = 0;
                    }
                }

                DeviceSnapshot snap;
                snap.read_bytes  = read_bytes;
                snap.write_bytes = write_bytes;
                snap.read_ios    = read_ops;
                snap.write_ios   = write_ops;
                snap.timestamp   = now;
                previous_[bsd_name] = snap;

                // Only include actual disk devices (e.g. disk0, disk1)
                if (bsd_name.rfind("disk", 0) == 0 && bsd_name.size() > 4 && std::isdigit(bsd_name[4])) {
                    result.push_back(ios);
                }
            }
            CFRelease(props);
        }

        IOObjectRelease(drive);
    }
    IOObjectRelease(iterator);

    std::sort(result.begin(), result.end(), [](const DiskIOStats& a, const DiskIOStats& b) {
        return a.device < b.device;
    });

    return result;
}

#else
std::vector<DiskIOStats> DiskIOMonitor::read_macos() { return {}; }
#endif
