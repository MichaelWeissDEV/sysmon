#include <gtest/gtest.h>
#include "sysmon/disk_monitor.hpp"

TEST(DiskMonitorTest, ReturnsAtLeastRoot) {
    DiskMonitor mon;
    auto disks = mon.read();
    EXPECT_FALSE(disks.empty()) << "Should find at least one mounted filesystem";
}

TEST(DiskMonitorTest, AllDisksHavePositiveTotal) {
    DiskMonitor mon;
    auto disks = mon.read();
    for (const auto& d : disks) {
        EXPECT_GT(d.total_bytes, 0u) << "Disk " << d.mountpoint << " has zero total bytes";
    }
}

TEST(DiskMonitorTest, UsagePercentInRange) {
    DiskMonitor mon;
    auto disks = mon.read();
    for (const auto& d : disks) {
        EXPECT_GE(d.usage_percent, 0.0);
        EXPECT_LE(d.usage_percent, 100.0);
    }
}

// ---------------------------------------------------------------------------
// Deduplication and shadow-mount filtering
// ---------------------------------------------------------------------------

namespace {

DiskStats make_disk(const std::string& mountpoint, const std::string& device,
                    uint64_t total = 1000) {
    DiskStats d;
    d.mountpoint  = mountpoint;
    d.device      = device;
    d.total_bytes = total;
    return d;
}

} // namespace

TEST(DiskMonitorTest, DeduplicateKeepsShortestMountpointPerDevice) {
    // macOS mounts the same APFS system volume at "/" and again under
    // /System/Volumes/Update; reporting both double-counts the storage.
    std::vector<DiskStats> disks = {
        make_disk("/", "/dev/disk3s1s1"),
        make_disk("/System/Volumes/Update/mnt1", "/dev/disk3s1s1"),
        make_disk("/Volumes/External", "/dev/disk5s1"),
    };

    const auto result = DiskMonitor::deduplicate(disks);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].mountpoint, "/");
    EXPECT_EQ(result[1].mountpoint, "/Volumes/External");
}

TEST(DiskMonitorTest, DeduplicateKeepsEntriesWithoutADevice) {
    std::vector<DiskStats> disks = {
        make_disk("/a", ""),
        make_disk("/b", ""),
    };
    EXPECT_EQ(DiskMonitor::deduplicate(disks).size(), 2u);
}

TEST(DiskMonitorTest, ShadowMountpointsAreRecognised) {
    EXPECT_TRUE(DiskMonitor::is_shadow_mountpoint("/System/Volumes/Data"));
    EXPECT_TRUE(DiskMonitor::is_shadow_mountpoint("/System/Volumes/Update/SFR/mnt1"));
    EXPECT_TRUE(DiskMonitor::is_shadow_mountpoint("/snap/core/1234"));
    EXPECT_FALSE(DiskMonitor::is_shadow_mountpoint("/"));
    EXPECT_FALSE(DiskMonitor::is_shadow_mountpoint("/home"));
    EXPECT_FALSE(DiskMonitor::is_shadow_mountpoint("/Volumes/Backup"));
}

TEST(DiskMonitorTest, VirtualFilesystemsAreRecognised) {
    EXPECT_TRUE(DiskMonitor::is_virtual_filesystem("tmpfs"));
    EXPECT_TRUE(DiskMonitor::is_virtual_filesystem("devfs"));
    EXPECT_FALSE(DiskMonitor::is_virtual_filesystem("ext4"));
    EXPECT_FALSE(DiskMonitor::is_virtual_filesystem("apfs"));
    EXPECT_FALSE(DiskMonitor::is_virtual_filesystem("NTFS"));
}

TEST(DiskMonitorTest, ReportedUsageNeverExceedsCapacity) {
    // A filesystem that reports more free space than total must not wrap around
    // to a nonsensical "used" figure.
    DiskMonitor monitor;
    for (const auto& d : monitor.read()) {
        EXPECT_LE(d.used_bytes, d.total_bytes) << d.mountpoint;
        EXPECT_GE(d.usage_percent, 0.0) << d.mountpoint;
        EXPECT_LE(d.usage_percent, 100.0) << d.mountpoint;
    }
}
