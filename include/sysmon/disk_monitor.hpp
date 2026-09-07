/**
 * @file disk_monitor.hpp
 * @brief Filesystem usage monitor.
 */

#ifndef SYSMON_DISK_MONITOR_HPP
#define SYSMON_DISK_MONITOR_HPP

#include "sysmon/stats.hpp"
#include <string>
#include <vector>

/**
 * @brief Reads disk / filesystem usage statistics.
 *
 * Pseudo-filesystems are skipped, and volumes that are only different views of
 * the same underlying storage (APFS firmlinks, bind mounts) are reported once.
 */
class DiskMonitor {
public:
    /**
     * @brief Read all mounted filesystems (excluding virtual ones).
     */
    std::vector<DiskStats> read();

    /** @brief True for kernel/pseudo filesystems that hold no real storage. */
    static bool is_virtual_filesystem(const std::string& fs_type);

    /**
     * @brief True for mountpoints that only mirror another volume.
     *
     * macOS mounts the system volume, its data volume and several snapshot
     * views into /System/Volumes; listing them all would report the same
     * hundreds of gigabytes four times over.
     */
    static bool is_shadow_mountpoint(const std::string& mountpoint);

    /**
     * @brief Drop entries that describe storage already covered by another.
     *
     * Keeps the entry with the shortest mountpoint for each backing device.
     * Exposed for testing.
     */
    static std::vector<DiskStats> deduplicate(std::vector<DiskStats> disks);
};

#endif // SYSMON_DISK_MONITOR_HPP
