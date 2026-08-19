/**
 * @file gpu_monitor.hpp
 * @brief GPU / Integrated-Graphics monitor.
 */

#ifndef SYSMON_GPU_MONITOR_HPP
#define SYSMON_GPU_MONITOR_HPP

#include "sysmon/stats.hpp"
#include "sysmon/platform.hpp"
#include <vector>

/**
 * @brief Collects GPU statistics.
 *
 * Supports:
 * - Apple Silicon (Unified Memory, GPU core count via sysctl)
 * - Linux AMD (AMDGPU driver via /sys/class/drm/)
 * - Linux NVIDIA (nouveau / nvidia driver via /sys/class/drm/ and sysfs)
 * - Linux Intel (i915 via /sys/class/drm/)
 */
class GpuMonitor {
public:
    GpuMonitor() = default;

    /**
     * @brief Read GPU statistics for all detected GPUs.
     * @return Vector of GpuStats (may be empty if no GPU detected).
     */
    std::vector<GpuStats> read();

private:
    std::vector<GpuStats> read_apple();
    std::vector<GpuStats> read_amd_linux();
    std::vector<GpuStats> read_nvidia_linux();
    std::vector<GpuStats> read_intel_linux();
};

#endif // SYSMON_GPU_MONITOR_HPP
