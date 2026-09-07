#include "sysmon/disk_io_monitor.hpp"
#include "sysmon/utils.hpp"
#include "sysmon/platform.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>
#include <filesystem>

#if defined(SYSMON_WINDOWS)
#  include <windows.h>
#  include <winioctl.h>
#endif

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
#elif defined(SYSMON_WINDOWS)
    return read_windows();
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

        // The first 10 stat fields are guaranteed; the timing and busy fields
        // that follow are not published by every device or kernel version, so
        // require only the base set and treat the rest as optional.
        if (parts.size() < 10) continue;

        std::string dev = parts[2];
        if (!is_physical_device(dev)) continue;

        auto field = [&parts](size_t i) -> uint64_t {
            if (i >= parts.size()) return 0;
            const auto v = utils::to_int(parts[i]);
            return (v.has_value() && *v >= 0) ? static_cast<uint64_t>(*v) : 0ULL;
        };

        const uint64_t reads_completed  = field(3);
        const uint64_t sectors_read     = field(5);
        const uint64_t writes_completed = field(7);
        const uint64_t sectors_written  = field(9);
        const bool     have_timing      = parts.size() >= 14;
        const uint64_t read_time_ms     = have_timing ? field(6)  : 0;
        const uint64_t write_time_ms    = have_timing ? field(10) : 0;
        const uint64_t busy_time_ms     = have_timing ? field(12) : 0;

        constexpr uint64_t sector_size = 512;
        uint64_t read_bytes    = sectors_read    * sector_size;
        uint64_t written_bytes = sectors_written * sector_size;

        DiskIOStats ios;
        ios.device            = dev;
        ios.read_bytes_total  = read_bytes;
        ios.write_bytes_total = written_bytes;
        ios.read_ops_total    = reads_completed;
        ios.write_ops_total   = writes_completed;

        auto it = previous_.find(dev);
        if (it != previous_.end()) {
            const double dt = std::chrono::duration<double>(now - it->second.timestamp).count();
            if (dt > 0.0) {
                auto rate = [dt](uint64_t current, uint64_t before) {
                    return current >= before ? static_cast<double>(current - before) / dt : 0.0;
                };
                ios.read_bytes_per_sec  = rate(read_bytes,      it->second.read_bytes);
                ios.write_bytes_per_sec = rate(written_bytes,   it->second.write_bytes);
                ios.read_ops_per_sec    = rate(reads_completed, it->second.read_ios);
                ios.write_ops_per_sec   = rate(writes_completed, it->second.write_ios);

                // Utilisation is the fraction of wall-clock time the device
                // had at least one request in flight.
                if (have_timing && busy_time_ms >= it->second.busy_time_ms) {
                    const double busy_s = static_cast<double>(busy_time_ms - it->second.busy_time_ms) / 1000.0;
                    ios.util_percent = std::min(100.0, busy_s / dt * 100.0);
                }
                // Average service time per completed request in this interval.
                const uint64_t d_reads = reads_completed >= it->second.read_ios
                                       ? reads_completed - it->second.read_ios : 0;
                if (have_timing && d_reads > 0 && read_time_ms >= it->second.read_time_ns) {
                    ios.avg_read_latency_ms =
                        static_cast<double>(read_time_ms - it->second.read_time_ns) /
                        static_cast<double>(d_reads);
                }
                const uint64_t d_writes = writes_completed >= it->second.write_ios
                                        ? writes_completed - it->second.write_ios : 0;
                if (have_timing && d_writes > 0 && write_time_ms >= it->second.write_time_ns) {
                    ios.avg_write_latency_ms =
                        static_cast<double>(write_time_ms - it->second.write_time_ns) /
                        static_cast<double>(d_writes);
                }
            }
        }

        DeviceSnapshot snap;
        snap.read_bytes    = read_bytes;
        snap.write_bytes   = written_bytes;
        snap.read_ios      = reads_completed;
        snap.write_ios     = writes_completed;
        snap.read_time_ns  = read_time_ms;
        snap.write_time_ns = write_time_ms;
        snap.busy_time_ms  = busy_time_ms;
        snap.timestamp     = now;
        previous_[dev]     = snap;

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
                ios.device            = bsd_name;
                ios.read_bytes_total  = read_bytes;
                ios.write_bytes_total = write_bytes;
                ios.read_ops_total    = read_ops;
                ios.write_ops_total   = write_ops;

                auto it = previous_.find(bsd_name);
                if (it != previous_.end()) {
                    double dt = std::chrono::duration<double>(now - it->second.timestamp).count();
                    if (dt > 0.0) {
                        if (read_bytes >= it->second.read_bytes) {
                            ios.read_bytes_per_sec = static_cast<double>(read_bytes - it->second.read_bytes) / dt;
                        }
                        if (write_bytes >= it->second.write_bytes) {
                            ios.write_bytes_per_sec = static_cast<double>(write_bytes - it->second.write_bytes) / dt;
                        }
                        if (read_ops >= it->second.read_ios) {
                            ios.read_ops_per_sec = static_cast<double>(read_ops - it->second.read_ios) / dt;
                        }
                        if (write_ops >= it->second.write_ios) {
                            ios.write_ops_per_sec = static_cast<double>(write_ops - it->second.write_ios) / dt;
                        }
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

// ---------------------------------------------------------------------------
// Windows: IOCTL_DISK_PERFORMANCE per physical drive
// ---------------------------------------------------------------------------

#if defined(SYSMON_WINDOWS)

std::vector<DiskIOStats> DiskIOMonitor::read_windows() {
    std::vector<DiskIOStats> result;
    const auto now = std::chrono::steady_clock::now();

    // Physical drive numbers are dense in practice; stop after the first gap
    // beyond a small tolerance so a removed drive does not end the scan early.
    int misses = 0;
    for (int index = 0; index < 32 && misses < 4; ++index) {
        const std::string path = "\\\\.\\PhysicalDrive" + std::to_string(index);

        // Zero desired access opens the device for metadata only, which is
        // what IOCTL_DISK_PERFORMANCE needs and does not require elevation.
        HANDLE handle = CreateFileA(path.c_str(), 0,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            ++misses;
            continue;
        }
        misses = 0;

        DISK_PERFORMANCE perf{};
        DWORD returned = 0;
        const BOOL ok = DeviceIoControl(handle, IOCTL_DISK_PERFORMANCE, nullptr, 0,
                                        &perf, sizeof(perf), &returned, nullptr);
        CloseHandle(handle);
        if (!ok) continue;

        const std::string dev = "PhysicalDrive" + std::to_string(index);
        const auto read_bytes  = static_cast<uint64_t>(perf.BytesRead.QuadPart);
        const auto write_bytes = static_cast<uint64_t>(perf.BytesWritten.QuadPart);
        const auto read_ops    = static_cast<uint64_t>(perf.ReadCount);
        const auto write_ops   = static_cast<uint64_t>(perf.WriteCount);
        // Times are in 100 ns units.
        const auto read_time   = static_cast<uint64_t>(perf.ReadTime.QuadPart);
        const auto write_time  = static_cast<uint64_t>(perf.WriteTime.QuadPart);
        const auto idle_time   = static_cast<uint64_t>(perf.IdleTime.QuadPart);

        DiskIOStats ios;
        ios.device            = dev;
        ios.read_bytes_total  = read_bytes;
        ios.write_bytes_total = write_bytes;
        ios.read_ops_total    = read_ops;
        ios.write_ops_total   = write_ops;
        ios.queue_depth       = static_cast<double>(perf.QueueDepth);

        auto it = previous_.find(dev);
        if (it != previous_.end()) {
            const double dt = std::chrono::duration<double>(now - it->second.timestamp).count();
            if (dt > 0.0) {
                auto rate = [dt](uint64_t current, uint64_t before) {
                    return current >= before ? static_cast<double>(current - before) / dt : 0.0;
                };
                ios.read_bytes_per_sec  = rate(read_bytes,  it->second.read_bytes);
                ios.write_bytes_per_sec = rate(write_bytes, it->second.write_bytes);
                ios.read_ops_per_sec    = rate(read_ops,    it->second.read_ios);
                ios.write_ops_per_sec   = rate(write_ops,   it->second.write_ios);

                const uint64_t d_reads = read_ops >= it->second.read_ios
                                       ? read_ops - it->second.read_ios : 0;
                if (d_reads > 0 && read_time >= it->second.read_time_ns) {
                    ios.avg_read_latency_ms =
                        static_cast<double>(read_time - it->second.read_time_ns) /
                        static_cast<double>(d_reads) / 10000.0;
                }
                const uint64_t d_writes = write_ops >= it->second.write_ios
                                        ? write_ops - it->second.write_ios : 0;
                if (d_writes > 0 && write_time >= it->second.write_time_ns) {
                    ios.avg_write_latency_ms =
                        static_cast<double>(write_time - it->second.write_time_ns) /
                        static_cast<double>(d_writes) / 10000.0;
                }
                // Busy fraction is whatever was not idle during the interval.
                if (idle_time >= it->second.busy_time_ms) {
                    const double idle_s =
                        static_cast<double>(idle_time - it->second.busy_time_ms) / 1e7;
                    ios.util_percent = std::max(0.0, std::min(100.0, (1.0 - idle_s / dt) * 100.0));
                }
            }
        }

        DeviceSnapshot snap;
        snap.read_bytes    = read_bytes;
        snap.write_bytes   = write_bytes;
        snap.read_ios      = read_ops;
        snap.write_ios     = write_ops;
        snap.read_time_ns  = read_time;
        snap.write_time_ns = write_time;
        snap.busy_time_ms  = idle_time;
        snap.timestamp     = now;
        previous_[dev]     = snap;

        result.push_back(std::move(ios));
    }

    return result;
}

#else
std::vector<DiskIOStats> DiskIOMonitor::read_windows() { return {}; }
#endif
