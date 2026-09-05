#include "sysmon/system_monitor.hpp"
#include "sysmon/platform.hpp"
#include "sysmon/utils.hpp"

#include <cstring>
#include <ctime>
#include <sstream>

#if defined(SYSMON_WINDOWS)
#  include <windows.h>
#  include <lm.h>
#  include <wtsapi32.h>
#else
#  include <sys/utsname.h>
#  include <unistd.h>
#  include <utmpx.h>
#endif

#if defined(SYSMON_MACOS)
#  include <sys/sysctl.h>
#  include <sys/types.h>
#  include <mach/mach_host.h>
#endif

#if defined(SYSMON_LINUX)
#  include <sys/sysinfo.h>
#endif

namespace {

/** @brief Local timezone abbreviation, e.g. "CEST". */
std::string current_timezone() {
    std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
#if defined(SYSMON_WINDOWS)
    if (localtime_s(&tm_buf, &now) != 0) return "";
#else
    if (localtime_r(&now, &tm_buf) == nullptr) return "";
#endif
    char buf[64];
    if (std::strftime(buf, sizeof(buf), "%Z", &tm_buf) == 0) return "";
    return buf;
}

#if defined(SYSMON_POSIX)
/** @brief Count distinct users with an active login session. */
std::optional<unsigned int> count_logged_in_users() {
    unsigned int count = 0;
    setutxent();
    while (struct utmpx* entry = getutxent()) {
        if (entry->ut_type == USER_PROCESS) ++count;
    }
    endutxent();
    return count;
}
#endif

#if defined(SYSMON_LINUX)
/**
 * @brief Best-effort hypervisor / container detection.
 *
 * Everything here is a positive signal only: when none of the markers is
 * present we report "none" rather than claiming bare metal with certainty.
 */
std::string detect_virtualization_linux() {
    if (utils::read_file("/.dockerenv").has_value()) return "Docker";

    if (auto cgroup = utils::read_file("/proc/1/cgroup")) {
        const std::string& c = cgroup.value();
        if (c.find("docker")     != std::string::npos) return "Docker";
        if (c.find("containerd") != std::string::npos) return "containerd";
        if (c.find("lxc")        != std::string::npos) return "LXC";
        if (c.find("kubepods")   != std::string::npos) return "Kubernetes";
    }
    if (auto env = utils::read_first_line("/run/systemd/container")) {
        if (!env->empty()) return *env;
    }

    // DMI strings are the most reliable hypervisor marker on x86.
    for (const char* path : {"/sys/class/dmi/id/product_name",
                             "/sys/class/dmi/id/sys_vendor"}) {
        if (auto v = utils::read_first_line(path)) {
            const std::string lower = utils::to_lower(*v);
            if (lower.find("kvm")        != std::string::npos) return "KVM";
            if (lower.find("qemu")       != std::string::npos) return "QEMU";
            if (lower.find("vmware")     != std::string::npos) return "VMware";
            if (lower.find("virtualbox") != std::string::npos) return "VirtualBox";
            if (lower.find("xen")        != std::string::npos) return "Xen";
            if (lower.find("hyper-v")    != std::string::npos ||
                lower.find("microsoft")  != std::string::npos) return "Hyper-V";
            if (lower.find("parallels")  != std::string::npos) return "Parallels";
            if (lower.find("bhyve")      != std::string::npos) return "bhyve";
        }
    }
    if (auto hv = utils::read_first_line("/sys/hypervisor/type")) {
        if (!hv->empty()) return *hv;
    }
    return "none";
}
#endif

} // namespace

// ---------------------------------------------------------------------------
// Parsing helpers (platform independent, unit tested)
// ---------------------------------------------------------------------------

std::string SystemMonitor::parse_os_release(const std::string& content) {
    std::istringstream iss(content);
    std::string line;
    std::string name;
    std::string version;

    auto unquote = [](std::string value) {
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        return value;
    };

    while (std::getline(iss, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = utils::trim(line.substr(0, eq));
        const std::string value = unquote(utils::trim(line.substr(eq + 1)));

        if (key == "PRETTY_NAME" && !value.empty()) return value;
        if (key == "NAME")    name    = value;
        if (key == "VERSION") version = value;
    }

    if (!name.empty() && !version.empty()) return name + " " + version;
    return name;
}

// ---------------------------------------------------------------------------
// Main read
// ---------------------------------------------------------------------------

SystemStats SystemMonitor::read() {
    SystemStats stats;

    stats.current_time = utils::format_time(static_cast<long long>(std::time(nullptr)));
    stats.timezone     = current_timezone();
    stats.os           = get_os_name();
    stats.architecture = get_architecture();

#if defined(SYSMON_WINDOWS)
    // -- Hostname ----------------------------------------------------------
    {
        char name[MAX_COMPUTERNAME_LENGTH + 1] = {};
        DWORD len = sizeof(name);
        if (GetComputerNameA(name, &len)) stats.hostname = name;
    }

    // -- Kernel / build ----------------------------------------------------
    {
        // The Win32 version APIs lie for compatibility; the registry does not.
        HKEY key{};
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                          "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                          0, KEY_READ, &key) == ERROR_SUCCESS) {
            char buf[256];
            DWORD size = sizeof(buf);
            DWORD type = 0;
            if (RegQueryValueExA(key, "CurrentBuildNumber", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS) {
                stats.os_build = std::string(buf, strnlen(buf, size));
                // ProductName still reads "Windows 10" on Windows 11; only the
                // build number distinguishes them.
                const auto build = utils::to_int(stats.os_build);
                if (build.has_value() && *build >= 22000 &&
                    stats.os.find("Windows 10") != std::string::npos) {
                    stats.os.replace(stats.os.find("Windows 10"),
                                     std::strlen("Windows 10"), "Windows 11");
                }
            }
            size = sizeof(buf);
            if (RegQueryValueExA(key, "DisplayVersion", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS) {
                const std::string display(buf, strnlen(buf, size));
                if (!display.empty()) stats.os += " " + display;
            }
            RegCloseKey(key);
        }

        // RtlGetVersion reports the true NT version, unlike GetVersionEx.
        using RtlGetVersionFn = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
        if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
            auto rtl = reinterpret_cast<RtlGetVersionFn>(
                reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
            RTL_OSVERSIONINFOW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (rtl != nullptr && rtl(&info) == 0) {
                stats.kernel = "NT " + std::to_string(info.dwMajorVersion) + "." +
                               std::to_string(info.dwMinorVersion) + "." +
                               std::to_string(info.dwBuildNumber);
            }
        }
        if (stats.kernel.empty() && !stats.os_build.empty()) {
            stats.kernel = "NT build " + stats.os_build;
        }
    }

    // -- Machine model -----------------------------------------------------
    {
        HKEY key{};
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS",
                          0, KEY_READ, &key) == ERROR_SUCCESS) {
            char manufacturer[128] = {};
            char product[128]      = {};
            DWORD size = sizeof(manufacturer);
            RegQueryValueExA(key, "SystemManufacturer", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(manufacturer), &size);
            size = sizeof(product);
            RegQueryValueExA(key, "SystemProductName", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(product), &size);
            RegCloseKey(key);

            std::string model = utils::trim(manufacturer);
            const std::string prod = utils::trim(product);
            if (!prod.empty()) model = model.empty() ? prod : model + " " + prod;
            stats.machine_model = model;
        }
    }

    // -- Uptime and boot time ---------------------------------------------
    {
        const ULONGLONG ms = GetTickCount64();
        stats.uptime_seconds = static_cast<double>(ms) / 1000.0;
        stats.uptime = utils::format_duration_seconds(stats.uptime_seconds);
        stats.boot_time = utils::format_time(
            static_cast<long long>(std::time(nullptr)) -
            static_cast<long long>(stats.uptime_seconds));
    }

    // -- Page size ---------------------------------------------------------
    {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        stats.page_size_bytes = static_cast<uint64_t>(si.dwPageSize);
    }

    // -- Logged-in users ---------------------------------------------------
    {
        PWTS_SESSION_INFOA sessions = nullptr;
        DWORD count = 0;
        if (WTSEnumerateSessionsA(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count)) {
            unsigned int active = 0;
            for (DWORD i = 0; i < count; ++i) {
                if (sessions[i].State == WTSActive) ++active;
            }
            stats.logged_in_users = active;
            WTSFreeMemory(sessions);
        }
    }

    // -- Virtualization ----------------------------------------------------
    {
        // Bit 31 of CPUID leaf 1 ECX is the hypervisor-present flag.
        stats.virtualization = "none";
        HKEY key{};
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS",
                          0, KEY_READ, &key) == ERROR_SUCCESS) {
            char vendor[128] = {};
            DWORD size = sizeof(vendor);
            if (RegQueryValueExA(key, "SystemManufacturer", nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(vendor), &size) == ERROR_SUCCESS) {
                const std::string lower = utils::to_lower(utils::trim(vendor));
                if (lower.find("vmware")     != std::string::npos) stats.virtualization = "VMware";
                else if (lower.find("qemu")  != std::string::npos) stats.virtualization = "QEMU";
                else if (lower.find("innotek") != std::string::npos ||
                         lower.find("virtualbox") != std::string::npos) stats.virtualization = "VirtualBox";
                else if (lower.find("microsoft") != std::string::npos) stats.virtualization = "Hyper-V";
                else if (lower.find("parallels") != std::string::npos) stats.virtualization = "Parallels";
                else if (lower.find("xen")   != std::string::npos) stats.virtualization = "Xen";
            }
            RegCloseKey(key);
        }
    }

#else  // ------------------------------------------------------------------ POSIX

    // -- Hostname ----------------------------------------------------------
    {
        char host[256] = {};
        if (gethostname(host, sizeof(host) - 1) == 0) stats.hostname = host;
    }

    // -- Kernel ------------------------------------------------------------
    {
        struct utsname uts{};
        if (uname(&uts) == 0) {
            stats.kernel = uts.release;
            if (stats.architecture.empty()) stats.architecture = uts.machine;
        }
    }

    stats.page_size_bytes  = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
    stats.logged_in_users  = count_logged_in_users();

#  if defined(SYSMON_LINUX)
    // -- Uptime ------------------------------------------------------------
    if (auto uptime = utils::read_file("/proc/uptime")) {
        const auto parts = utils::split_whitespace(uptime.value());
        if (!parts.empty()) {
            if (auto secs = utils::to_double(parts[0])) {
                stats.uptime_seconds = secs.value();
                stats.uptime = utils::format_duration_seconds(secs.value());
            }
        }
    }

    // -- Process / thread counts ------------------------------------------
    {
        struct sysinfo info{};
        if (sysinfo(&info) == 0) {
            stats.process_count = static_cast<unsigned int>(info.procs);
        }
    }
    if (auto loadavg = utils::read_file("/proc/loadavg")) {
        const auto parts = utils::split_whitespace(loadavg.value());
        if (parts.size() >= 4) {
            const auto slash = parts[3].find('/');
            if (slash != std::string::npos) {
                if (auto total = utils::to_int(parts[3].substr(slash + 1))) {
                    stats.thread_count = static_cast<unsigned int>(*total);
                }
            }
        }
    }

    stats.machine_model = utils::read_first_line("/sys/class/dmi/id/product_name").value_or("");
    if (stats.machine_model.empty()) {
        stats.machine_model = utils::read_first_line("/proc/device-tree/model").value_or("");
    }
    stats.virtualization = detect_virtualization_linux();

#  elif defined(SYSMON_MACOS)
    // -- Uptime from the kernel boot timestamp -----------------------------
    {
        struct timeval boot{};
        size_t len = sizeof(boot);
        int mib[2] = {CTL_KERN, KERN_BOOTTIME};
        if (sysctl(mib, 2, &boot, &len, nullptr, 0) == 0 && boot.tv_sec != 0) {
            const long long now = static_cast<long long>(std::time(nullptr));
            stats.uptime_seconds = static_cast<double>(now - boot.tv_sec);
            stats.uptime    = utils::format_duration_seconds(stats.uptime_seconds);
            stats.boot_time = utils::format_time(boot.tv_sec);
        }
    }

    // -- Machine model -----------------------------------------------------
    {
        char model[256] = {};
        size_t len = sizeof(model);
        if (sysctlbyname("hw.model", model, &len, nullptr, 0) == 0) {
            stats.machine_model = model;
        }
    }

    // -- Build number ------------------------------------------------------
    if (auto build = utils::run_command("sw_vers -buildVersion")) {
        stats.os_build = utils::trim(*build);
    }

    // -- Process count -----------------------------------------------------
    {
        int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
        size_t size = 0;
        if (sysctl(mib, 4, nullptr, &size, nullptr, 0) == 0 && size > 0) {
            stats.process_count =
                static_cast<unsigned int>(size / sizeof(struct kinfo_proc));
        }
    }

    // -- Virtualization ----------------------------------------------------
    {
        int vmm = 0;
        size_t len = sizeof(vmm);
        if (sysctlbyname("kern.hv_vmm_present", &vmm, &len, nullptr, 0) == 0 && vmm != 0) {
            stats.virtualization = "Hypervisor";
        } else {
            stats.virtualization = "none";
        }
    }
#  endif

    if (stats.boot_time.empty() && stats.uptime_seconds > 0) {
        stats.boot_time = utils::format_time(
            static_cast<long long>(std::time(nullptr)) -
            static_cast<long long>(stats.uptime_seconds));
    }
#endif // SYSMON_WINDOWS

    if (stats.hostname.empty()) stats.hostname = "unknown";
    if (stats.uptime.empty())   stats.uptime   = "unknown";

    return stats;
}

// ---------------------------------------------------------------------------
// OS name / architecture
// ---------------------------------------------------------------------------

std::string SystemMonitor::get_os_name() {
#if defined(SYSMON_LINUX)
    if (auto content = utils::read_file("/etc/os-release")) {
        const std::string name = parse_os_release(content.value());
        if (!name.empty()) return name;
    }
    return "Linux";

#elif defined(SYSMON_MACOS)
    std::string product;
    std::string version;
    if (auto p = utils::run_command("sw_vers -productName"))    product = utils::trim(*p);
    if (auto v = utils::run_command("sw_vers -productVersion")) version = utils::trim(*v);

    if (product.empty()) product = "macOS";
    if (version.empty()) {
        char osversion[64] = {};
        size_t len = sizeof(osversion);
        if (sysctlbyname("kern.osproductversion", osversion, &len, nullptr, 0) == 0) {
            version = osversion;
        }
    }
    return version.empty() ? product : product + " " + version;

#elif defined(SYSMON_WINDOWS)
    HKEY key{};
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        char buf[256] = {};
        DWORD size = sizeof(buf);
        LSTATUS rc = RegQueryValueExA(key, "ProductName", nullptr, nullptr,
                                      reinterpret_cast<LPBYTE>(buf), &size);
        RegCloseKey(key);
        if (rc == ERROR_SUCCESS) {
            return std::string(buf, strnlen(buf, size));
        }
    }
    return "Windows";

#else
    return SYSMON_PLATFORM_NAME;
#endif
}

std::string SystemMonitor::get_architecture() {
#if defined(SYSMON_WINDOWS)
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return "x86_64";
        case PROCESSOR_ARCHITECTURE_ARM:   return "arm";
        case PROCESSOR_ARCHITECTURE_ARM64: return "arm64";
        case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
        default:                           return "unknown";
    }
#else
    struct utsname uts{};
    if (uname(&uts) == 0) return uts.machine;
    return "unknown";
#endif
}
