#include "sysmon/system_monitor.hpp"
#include "sysmon/utils.hpp"
#include "sysmon/platform.hpp"

#include <unistd.h>
#include <sys/utsname.h>
#include <fstream>
#include <sstream>
#include <algorithm>

#if defined(SYSMON_MACOS)
#  include <sys/sysctl.h>
#  include <sys/time.h>
#endif

SystemStats SystemMonitor::read() {
    SystemStats stats;

    // Hostname
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        stats.hostname = hostname;
    } else {
        stats.hostname = "unknown";
    }

    // Kernel version via uname
    struct utsname buf;
    if (uname(&buf) == 0) {
        stats.kernel       = buf.release;
        stats.architecture = buf.machine;
    } else {
        stats.kernel       = "unknown";
        stats.architecture = "unknown";
    }

    // OS name
    stats.os = get_os_name();

    // Uptime
#if defined(SYSMON_LINUX)
    auto uptime_file = utils::read_file("/proc/uptime");
    if (uptime_file.has_value()) {
        std::istringstream iss(uptime_file.value());
        double uptime_sec = 0.0;
        iss >> uptime_sec;
        stats.uptime_seconds = uptime_sec;
        stats.uptime = utils::format_duration_seconds(uptime_sec);
    }
#elif defined(SYSMON_MACOS)
    // sysctl kern.boottime gives a timeval
    struct timeval boottime;
    size_t len = sizeof(boottime);
    if (sysctlbyname("kern.boottime", &boottime, &len, nullptr, 0) == 0) {
        struct timeval now;
        gettimeofday(&now, nullptr);
        double uptime_sec = static_cast<double>(now.tv_sec - boottime.tv_sec);
        stats.uptime_seconds = uptime_sec;
        stats.uptime = utils::format_duration_seconds(uptime_sec);
    }
#endif

    return stats;
}

std::string SystemMonitor::get_os_name() {
#if defined(SYSMON_LINUX)
    auto os_release = utils::read_file("/etc/os-release");
    if (os_release.has_value()) {
        std::istringstream iss(os_release.value());
        std::string line;
        while (std::getline(iss, line)) {
            if (line.substr(0, 12) == "PRETTY_NAME=") {
                std::string name = line.substr(12);
                // Strip surrounding quotes
                if (!name.empty() && name.front() == '"') {
                    name = name.substr(1);
                }
                if (!name.empty() && name.back() == '"') {
                    name.pop_back();
                }
                return utils::trim(name);
            }
        }
    }
    return "Linux";
#elif defined(SYSMON_MACOS)
    // Read macOS version from sw_vers-style plist
    std::string os_info = "macOS";
    auto product_name = utils::read_file("/System/Library/CoreServices/SystemVersion.plist");
    if (product_name.has_value()) {
        const std::string& plist = product_name.value();
        auto find_value = [&](const std::string& key) -> std::string {
            auto pos = plist.find("<key>" + key + "</key>");
            if (pos == std::string::npos) return "";
            pos = plist.find("<string>", pos);
            if (pos == std::string::npos) return "";
            pos += 8;
            auto end = plist.find("</string>", pos);
            if (end == std::string::npos) return "";
            return plist.substr(pos, end - pos);
        };
        std::string name    = find_value("ProductName");
        std::string version = find_value("ProductVersion");
        if (!name.empty() && !version.empty()) {
            os_info = name + " " + version;
        }
    }

    // Append Mac model identifier (e.g. Mac16,5)
    char model_buf[128] = {0};
    size_t mlen = sizeof(model_buf);
    if (sysctlbyname("hw.model", model_buf, &mlen, nullptr, 0) == 0 && model_buf[0] != '\0') {
        os_info += " (" + std::string(model_buf) + ")";
    }

    return os_info;
#else
    return "Unknown";
#endif
}

std::string SystemMonitor::get_architecture() {
    struct utsname buf;
    if (uname(&buf) == 0) {
        return buf.machine;
    }
    return "unknown";
}