#include "sysmon/disk_monitor.hpp"
#include "sysmon/utils.hpp"
#include "sysmon/platform.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <sys/statvfs.h>

#if defined(SYSMON_MACOS)
#  include <sys/param.h>
#  include <sys/mount.h>
#endif

std::vector<DiskStats> DiskMonitor::read() {
    std::vector<DiskStats> stats;

#if defined(SYSMON_LINUX)
    auto mounts = utils::read_file("/proc/mounts");
    if (!mounts.has_value()) return stats;

    std::istringstream iss(mounts.value());
    std::string line;
    std::vector<std::string> seen_mountpoints;

    while (std::getline(iss, line)) {
        auto parts = utils::split(line, ' ');
        if (parts.size() < 3) continue;

        std::string device    = parts[0];
        std::string mountpoint = parts[1];
        std::string fs_type   = parts[2];

        if (is_virtual_filesystem(fs_type)) continue;

        // Skip duplicates
        if (std::find(seen_mountpoints.begin(), seen_mountpoints.end(), mountpoint) !=
            seen_mountpoints.end()) continue;
        seen_mountpoints.push_back(mountpoint);

        struct statvfs buf;
        if (statvfs(mountpoint.c_str(), &buf) != 0) continue;

        DiskStats ds;
        ds.device          = device;
        ds.mountpoint      = mountpoint;
        ds.filesystem_type = fs_type;
        ds.total_bytes     = static_cast<uint64_t>(buf.f_blocks) * buf.f_frsize;
        ds.available_bytes = static_cast<uint64_t>(buf.f_bavail) * buf.f_frsize;
        ds.used_bytes      = ds.total_bytes -
                             static_cast<uint64_t>(buf.f_bfree) * buf.f_frsize;

        if (ds.total_bytes > 0) {
            ds.usage_percent = static_cast<double>(ds.used_bytes) /
                               static_cast<double>(ds.total_bytes) * 100.0;
        }

        if (ds.total_bytes > 0) {
            stats.push_back(ds);
        }
    }

#elif defined(SYSMON_MACOS)
    struct statfs* mounts_buf = nullptr;
    int count = getmntinfo(&mounts_buf, MNT_NOWAIT);
    for (int i = 0; i < count; ++i) {
        const struct statfs& m = mounts_buf[i];
        std::string fs_type = m.f_fstypename;

        static const std::vector<std::string> skip_types = {
            "devfs", "autofs", "synthetic", "nullfs"
        };
        if (std::find(skip_types.begin(), skip_types.end(), fs_type) != skip_types.end()) continue;

        std::string mountpoint = m.f_mntonname;
        // Skip internal APFS support subvolumes
        if (mountpoint == "/System/Volumes/VM" ||
            mountpoint == "/System/Volumes/Preboot" ||
            mountpoint == "/System/Volumes/Update" ||
            mountpoint == "/System/Volumes/xarts" ||
            mountpoint == "/System/Volumes/iSCPreboot" ||
            mountpoint == "/System/Volumes/Hardware") {
            continue;
        }

        DiskStats ds;
        ds.device          = m.f_mntfromname;
        ds.mountpoint      = mountpoint;
        ds.filesystem_type = fs_type;
        ds.total_bytes     = static_cast<uint64_t>(m.f_blocks) * m.f_bsize;
        ds.available_bytes = static_cast<uint64_t>(m.f_bavail) * m.f_bsize;
        ds.used_bytes      = ds.total_bytes -
                             static_cast<uint64_t>(m.f_bfree) * m.f_bsize;

        if (ds.total_bytes > 0) {
            ds.usage_percent = static_cast<double>(ds.used_bytes) /
                               static_cast<double>(ds.total_bytes) * 100.0;
            stats.push_back(ds);
        }
    }
#endif

    // Ensure root is first
    std::stable_sort(stats.begin(), stats.end(), [](const DiskStats& a, const DiskStats& b) {
        if (a.mountpoint == "/" && b.mountpoint != "/") return true;
        return false;
    });

    return stats;
}

bool DiskMonitor::is_virtual_filesystem(const std::string& fs_type) {
    static const std::vector<std::string> virtual_fs = {
        "proc", "sysfs", "tmpfs", "devtmpfs", "cgroup", "cgroup2",
        "debugfs", "securityfs", "selinuxfs", "autofs", "rpc_pipefs",
        "none", "devpts", "pstore", "configfs", "hugetlbfs", "mqueue",
        "tracefs", "bpf", "fusectl", "efivarfs", "binfmt_misc", "overlay",
        "nsfs", "ramfs"
    };
    return std::find(virtual_fs.begin(), virtual_fs.end(), fs_type) != virtual_fs.end();
}