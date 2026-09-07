#include "sysmon/disk_monitor.hpp"
#include "sysmon/platform.hpp"
#include "sysmon/utils.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <sstream>

#if defined(SYSMON_POSIX)
#  include <sys/statvfs.h>
#endif

#if defined(SYSMON_MACOS)
#  include <sys/param.h>
#  include <sys/mount.h>
#endif

#if defined(SYSMON_WINDOWS)
#  include <windows.h>
#endif

namespace {

/// Compute used/percent consistently, guarding against the underflow that a
/// filesystem reporting more free blocks than total blocks would otherwise
/// cause on unsigned arithmetic.
void finalize(DiskStats& ds, uint64_t free_bytes) {
    ds.free_bytes = free_bytes;
    ds.used_bytes = ds.total_bytes > free_bytes ? ds.total_bytes - free_bytes : 0;
    if (ds.total_bytes > 0) {
        ds.usage_percent = static_cast<double>(ds.used_bytes) /
                           static_cast<double>(ds.total_bytes) * 100.0;
        ds.usage_percent = std::max(0.0, std::min(100.0, ds.usage_percent));
    }
    if (ds.inodes_total.has_value() && *ds.inodes_total > 0 && ds.inodes_used.has_value()) {
        ds.inode_usage_percent = static_cast<double>(*ds.inodes_used) /
                                 static_cast<double>(*ds.inodes_total) * 100.0;
    }
}

} // namespace

bool DiskMonitor::is_virtual_filesystem(const std::string& fs_type) {
    static const std::vector<std::string> virtual_fs = {
        "proc", "sysfs", "tmpfs", "devtmpfs", "cgroup", "cgroup2",
        "debugfs", "securityfs", "selinuxfs", "autofs", "rpc_pipefs",
        "none", "devpts", "pstore", "configfs", "hugetlbfs", "mqueue",
        "tracefs", "bpf", "fusectl", "efivarfs", "binfmt_misc", "overlay",
        "nsfs", "ramfs", "devfs", "synthetic", "nullfs", "squashfs"
    };
    return std::find(virtual_fs.begin(), virtual_fs.end(), fs_type) != virtual_fs.end();
}

bool DiskMonitor::is_shadow_mountpoint(const std::string& mountpoint) {
    // macOS: the sealed system volume, the data volume and the update snapshots
    // all live in the same APFS container and report identical capacity.
    if (utils::starts_with(mountpoint, "/System/Volumes/")) return true;
    if (utils::starts_with(mountpoint, "/private/var/vm"))  return true;

    // Linux: container and snapshot plumbing.
    if (utils::starts_with(mountpoint, "/snap/"))           return true;
    if (utils::starts_with(mountpoint, "/var/lib/docker/")) return true;
    return false;
}

std::vector<DiskStats> DiskMonitor::deduplicate(std::vector<DiskStats> disks) {
    // Keep the shortest mountpoint per backing device: "/" wins over
    // "/System/Volumes/Update/mnt1" for the same /dev/disk3s1s1.
    std::map<std::string, size_t> best_for_device;
    for (size_t i = 0; i < disks.size(); ++i) {
        const std::string& device = disks[i].device;
        if (device.empty()) continue;

        auto it = best_for_device.find(device);
        if (it == best_for_device.end()) {
            best_for_device[device] = i;
        } else if (disks[i].mountpoint.size() < disks[it->second].mountpoint.size()) {
            it->second = i;
        }
    }

    std::vector<DiskStats> result;
    result.reserve(disks.size());
    for (size_t i = 0; i < disks.size(); ++i) {
        const std::string& device = disks[i].device;
        if (!device.empty()) {
            const auto it = best_for_device.find(device);
            if (it != best_for_device.end() && it->second != i) continue;
        }
        result.push_back(std::move(disks[i]));
    }
    return result;
}

std::vector<DiskStats> DiskMonitor::read() {
    std::vector<DiskStats> stats;

#if defined(SYSMON_LINUX)
    auto mounts = utils::read_file("/proc/mounts");
    if (!mounts.has_value()) return stats;

    std::istringstream iss(mounts.value());
    std::string line;
    std::vector<std::string> seen_mountpoints;

    while (std::getline(iss, line)) {
        const auto parts = utils::split_whitespace(line);
        if (parts.size() < 4) continue;

        const std::string device     = parts[0];
        const std::string mountpoint = parts[1];
        const std::string fs_type    = parts[2];
        const std::string options    = parts[3];

        if (is_virtual_filesystem(fs_type))  continue;
        if (is_shadow_mountpoint(mountpoint)) continue;
        if (std::find(seen_mountpoints.begin(), seen_mountpoints.end(), mountpoint) !=
            seen_mountpoints.end()) continue;
        seen_mountpoints.push_back(mountpoint);

        struct statvfs buf{};
        if (statvfs(mountpoint.c_str(), &buf) != 0) continue;

        DiskStats ds;
        ds.device          = device;
        ds.mountpoint      = mountpoint;
        ds.filesystem_type = fs_type;
        ds.mount_options   = options;
        ds.read_only       = options.compare(0, 2, "ro") == 0 ||
                             options.find(",ro,") != std::string::npos;
        ds.block_size      = static_cast<uint64_t>(buf.f_frsize);
        ds.total_bytes     = static_cast<uint64_t>(buf.f_blocks) * buf.f_frsize;
        ds.available_bytes = static_cast<uint64_t>(buf.f_bavail) * buf.f_frsize;

        if (buf.f_files > 0) {
            ds.inodes_total = static_cast<uint64_t>(buf.f_files);
            ds.inodes_free  = static_cast<uint64_t>(buf.f_ffree);
            ds.inodes_used  = buf.f_files > buf.f_ffree
                            ? static_cast<uint64_t>(buf.f_files - buf.f_ffree) : 0;
        }

        finalize(ds, static_cast<uint64_t>(buf.f_bfree) * buf.f_frsize);
        if (ds.total_bytes > 0) stats.push_back(ds);
    }

#elif defined(SYSMON_MACOS)
    struct statfs* mounts_buf = nullptr;
    const int count = getmntinfo(&mounts_buf, MNT_NOWAIT);
    for (int i = 0; i < count; ++i) {
        const struct statfs& m = mounts_buf[i];
        const std::string fs_type    = m.f_fstypename;
        const std::string mountpoint = m.f_mntonname;

        if (is_virtual_filesystem(fs_type))   continue;
        if (is_shadow_mountpoint(mountpoint)) continue;

        DiskStats ds;
        ds.device          = m.f_mntfromname;
        ds.mountpoint      = mountpoint;
        ds.filesystem_type = fs_type;
        ds.read_only       = (m.f_flags & MNT_RDONLY) != 0;
        ds.removable       = (m.f_flags & MNT_REMOVABLE) != 0;
        ds.block_size      = static_cast<uint64_t>(m.f_bsize);
        ds.total_bytes     = static_cast<uint64_t>(m.f_blocks) * m.f_bsize;
        ds.available_bytes = static_cast<uint64_t>(m.f_bavail) * m.f_bsize;

        if (m.f_files > 0) {
            ds.inodes_total = static_cast<uint64_t>(m.f_files);
            ds.inodes_free  = static_cast<uint64_t>(m.f_ffree);
            ds.inodes_used  = m.f_files > m.f_ffree
                            ? static_cast<uint64_t>(m.f_files - m.f_ffree) : 0;
        }

        std::string options;
        if (ds.read_only)                options += "ro";
        if (m.f_flags & MNT_LOCAL)       options += options.empty() ? "local" : ",local";
        if (m.f_flags & MNT_NOEXEC)      options += ",noexec";
        if (m.f_flags & MNT_NOSUID)      options += ",nosuid";
        if (m.f_flags & MNT_JOURNALED)   options += ",journaled";
        ds.mount_options = options;

        finalize(ds, static_cast<uint64_t>(m.f_bfree) * m.f_bsize);
        if (ds.total_bytes > 0) stats.push_back(ds);
    }

#elif defined(SYSMON_WINDOWS)
    {
        char drives[512] = {};
        const DWORD len = GetLogicalDriveStringsA(sizeof(drives) - 1, drives);
        for (const char* drive = drives;
             drive < drives + len && *drive != '\0';
             drive += strlen(drive) + 1) {

            const UINT type = GetDriveTypeA(drive);
            // Skip anything with no media or no meaningful capacity.
            if (type == DRIVE_NO_ROOT_DIR || type == DRIVE_UNKNOWN) continue;

            ULARGE_INTEGER available{}, total{}, free_bytes{};
            if (!GetDiskFreeSpaceExA(drive, &available, &total, &free_bytes)) continue;
            if (total.QuadPart == 0) continue;

            char volume_name[MAX_PATH + 1] = {};
            char fs_name[MAX_PATH + 1]     = {};
            DWORD serial = 0, max_component = 0, fs_flags = 0;
            GetVolumeInformationA(drive, volume_name, sizeof(volume_name) - 1,
                                  &serial, &max_component, &fs_flags,
                                  fs_name, sizeof(fs_name) - 1);

            // Cluster geometry gives the allocation unit, the closest
            // equivalent to a POSIX block size.
            DWORD sectors_per_cluster = 0, bytes_per_sector = 0;
            DWORD free_clusters = 0, total_clusters = 0;
            GetDiskFreeSpaceA(drive, &sectors_per_cluster, &bytes_per_sector,
                              &free_clusters, &total_clusters);

            DiskStats ds;
            ds.mountpoint      = drive;
            ds.device          = volume_name[0] != '\0' ? volume_name : drive;
            ds.filesystem_type = fs_name[0] != '\0' ? fs_name : "unknown";
            ds.read_only       = (fs_flags & FILE_READ_ONLY_VOLUME) != 0;
            ds.removable       = (type == DRIVE_REMOVABLE || type == DRIVE_CDROM);
            ds.total_bytes     = total.QuadPart;
            ds.available_bytes = available.QuadPart;
            if (bytes_per_sector > 0 && sectors_per_cluster > 0) {
                ds.block_size = static_cast<uint64_t>(bytes_per_sector) * sectors_per_cluster;
            }
            switch (type) {
                case DRIVE_REMOVABLE: ds.mount_options = "removable"; break;
                case DRIVE_REMOTE:    ds.mount_options = "network";   break;
                case DRIVE_CDROM:     ds.mount_options = "optical";   break;
                case DRIVE_RAMDISK:   ds.mount_options = "ramdisk";   break;
                default:              ds.mount_options = "fixed";     break;
            }

            finalize(ds, free_bytes.QuadPart);
            stats.push_back(ds);
        }
    }
#endif

    stats = deduplicate(std::move(stats));

    // Root (or C:) first, then alphabetical, so the layout is stable.
    std::stable_sort(stats.begin(), stats.end(), [](const DiskStats& a, const DiskStats& b) {
        const bool a_root = (a.mountpoint == "/" || a.mountpoint == "C:\\");
        const bool b_root = (b.mountpoint == "/" || b.mountpoint == "C:\\");
        if (a_root != b_root) return a_root;
        return a.mountpoint < b.mountpoint;
    });

    return stats;
}
