#include "sysmon/gpu_monitor.hpp"
#include "sysmon/utils.hpp"
#include "sysmon/platform.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <optional>

#if defined(SYSMON_MACOS)
#  include <sys/sysctl.h>
#  include <mach/mach.h>
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<GpuStats> GpuMonitor::read() {
    std::vector<GpuStats> result;

#if defined(SYSMON_MACOS)
    auto apple = read_apple();
    result.insert(result.end(), apple.begin(), apple.end());
#elif defined(SYSMON_LINUX)
    auto amd  = read_amd_linux();
    auto nvid = read_nvidia_linux();
    auto intl = read_intel_linux();
    result.insert(result.end(), amd.begin(),  amd.end());
    result.insert(result.end(), nvid.begin(), nvid.end());
    result.insert(result.end(), intl.begin(), intl.end());
#endif

    return result;
}

// ---------------------------------------------------------------------------
// Apple Silicon
// ---------------------------------------------------------------------------

#if defined(SYSMON_MACOS)

std::vector<GpuStats> GpuMonitor::read_apple() {
    GpuStats gs;
    gs.vendor      = "Apple";
    gs.memory_type = "Unified";

    // CPU brand gives us the chip name (e.g. "Apple M4 Max")
    char brand[256] = {};
    size_t brand_len = sizeof(brand);
    if (sysctlbyname("machdep.cpu.brand_string", brand, &brand_len, nullptr, 0) == 0) {
        gs.name = std::string(brand) + " GPU";
    } else {
        gs.name = "Apple GPU";
    }

    // GPU core count via sysctl (available on some macOS versions)
    // Key: "hw.gpu.count" — not always present; fallback to known chip defaults
    int gpu_cores = 0;
    size_t gc_len = sizeof(gpu_cores);
    if (sysctlbyname("hw.gpu.count", &gpu_cores, &gc_len, nullptr, 0) == 0 && gpu_cores > 0) {
        gs.gpu_cores = static_cast<unsigned int>(gpu_cores);
    } else {
        // Heuristic: parse chip name for known configurations
        std::string chip(brand);
        if      (chip.find("M4 Max") != std::string::npos)  gs.gpu_cores = 40;
        else if (chip.find("M4 Pro") != std::string::npos)  gs.gpu_cores = 20;
        else if (chip.find("M4 Ultra") != std::string::npos) gs.gpu_cores = 80;
        else if (chip.find("M4")     != std::string::npos)  gs.gpu_cores = 10;
        else if (chip.find("M3 Max") != std::string::npos)  gs.gpu_cores = 40;
        else if (chip.find("M3 Pro") != std::string::npos)  gs.gpu_cores = 18;
        else if (chip.find("M3 Ultra") != std::string::npos) gs.gpu_cores = 76;
        else if (chip.find("M3")     != std::string::npos)  gs.gpu_cores = 10;
        else if (chip.find("M2 Max") != std::string::npos)  gs.gpu_cores = 38;
        else if (chip.find("M2 Pro") != std::string::npos)  gs.gpu_cores = 19;
        else if (chip.find("M2 Ultra") != std::string::npos) gs.gpu_cores = 76;
        else if (chip.find("M2")     != std::string::npos)  gs.gpu_cores = 10;
        else if (chip.find("M1 Max") != std::string::npos)  gs.gpu_cores = 32;
        else if (chip.find("M1 Pro") != std::string::npos)  gs.gpu_cores = 16;
        else if (chip.find("M1 Ultra") != std::string::npos) gs.gpu_cores = 64;
        else if (chip.find("M1")     != std::string::npos)  gs.gpu_cores = 8;
    }

    // Unified memory: total system RAM is shared between CPU and GPU
    int64_t total_ram = 0;
    size_t  ram_len   = sizeof(total_ram);
    if (sysctlbyname("hw.memsize", &total_ram, &ram_len, nullptr, 0) == 0) {
        gs.memory_total_bytes = static_cast<uint64_t>(total_ram);
    }

    // Memory usage: from vm_statistics (pages used by GPU are "wired" + compressed)
    vm_size_t page_size = 0;
    host_page_size(mach_host_self(), &page_size);

    vm_statistics64_data_t vm;
    mach_msg_type_number_t cnt = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm), &cnt) == KERN_SUCCESS) {
        uint64_t ps = static_cast<uint64_t>(page_size);
        gs.memory_used_bytes = vm.wire_count * ps + vm.compressor_page_count * ps;
        gs.memory_free_bytes = vm.free_count  * ps;
    }
    if (gs.memory_total_bytes > 0) {
        gs.memory_usage_percent = static_cast<double>(gs.memory_used_bytes) /
                                  static_cast<double>(gs.memory_total_bytes) * 100.0;
    }

    // GPU usage: requires powermetrics (root) or IOKit SMC — return 0 with note
    // A future version can shell out to `powermetrics --samplers gpu_power -n 1`
    // and parse "GPU Active Residency" from its output.
    gs.usage_percent = 0.0; // n/a without root

    // GPU frequency: not easily available without IOKit/powermetrics
    gs.frequency_mhz = 0.0;

    return {gs};
}

#else
std::vector<GpuStats> GpuMonitor::read_apple() { return {}; }
#endif

// ---------------------------------------------------------------------------
// Linux AMD (amdgpu driver)
// ---------------------------------------------------------------------------

#if defined(SYSMON_LINUX)

std::vector<GpuStats> GpuMonitor::read_amd_linux() {
    std::vector<GpuStats> result;

    try {
        for (const auto& card : fs::directory_iterator("/sys/class/drm/")) {
            std::string cname = card.path().filename().string();
            if (cname.find("card") == std::string::npos) continue;
            // Skip render nodes and connectors (card0-HDMI-A-1 etc.)
            if (cname.find('-') != std::string::npos) continue;

            std::string dev_path = card.path().string() + "/device";
            if (!fs::exists(dev_path)) continue;

            // Check for AMD-specific files
            if (!fs::exists(dev_path + "/gpu_busy_percent")) continue;

            GpuStats gs;
            gs.vendor      = "AMD";
            gs.memory_type = "GDDR";

            // Name from vendor/device
            auto pci_id = utils::read_file(dev_path + "/device");
            auto ven_id = utils::read_file(dev_path + "/vendor");
            gs.name = "AMD GPU (" + cname + ")";

            // GPU usage
            auto busy = utils::read_file(dev_path + "/gpu_busy_percent");
            if (busy.has_value()) {
                try { gs.usage_percent = std::stod(utils::trim(busy.value())); } catch (...) {}
            }

            // VRAM total
            auto vram_total = utils::read_file(dev_path + "/mem_info_vram_total");
            if (vram_total.has_value()) {
                try { gs.memory_total_bytes = std::stoull(utils::trim(vram_total.value())); } catch (...) {}
            }

            // VRAM used
            auto vram_used = utils::read_file(dev_path + "/mem_info_vram_used");
            if (vram_used.has_value()) {
                try { gs.memory_used_bytes = std::stoull(utils::trim(vram_used.value())); } catch (...) {}
            }
            gs.memory_free_bytes = (gs.memory_total_bytes > gs.memory_used_bytes)
                                   ? gs.memory_total_bytes - gs.memory_used_bytes : 0;
            if (gs.memory_total_bytes > 0) {
                gs.memory_usage_percent = static_cast<double>(gs.memory_used_bytes) /
                                          static_cast<double>(gs.memory_total_bytes) * 100.0;
                gs.memory_type = "GDDR";
            }

            // hwmon: temperature + power
            try {
                for (const auto& hw : fs::directory_iterator(dev_path + "/hwmon/")) {
                    // Temperature
                    auto temp = utils::read_file(hw.path().string() + "/temp1_input");
                    if (temp.has_value()) {
                        try { gs.temperature_celsius = std::stod(utils::trim(temp.value())) / 1000.0; } catch (...) {}
                    }
                    // Power
                    auto pwr = utils::read_file(hw.path().string() + "/power1_average");
                    if (!pwr.has_value()) pwr = utils::read_file(hw.path().string() + "/power1_input");
                    if (pwr.has_value()) {
                        try { gs.power_watts = std::stod(utils::trim(pwr.value())) / 1e6; } catch (...) {}
                    }
                    // Core freq
                    auto freq = utils::read_file(hw.path().string() + "/freq1_input");
                    if (freq.has_value()) {
                        try { gs.frequency_mhz = std::stod(utils::trim(freq.value())) / 1e6; } catch (...) {}
                    }
                    break; // first hwmon entry
                }
            } catch (...) {}

            result.push_back(gs);
        }
    } catch (...) {}

    return result;
}

// ---------------------------------------------------------------------------
// Linux NVIDIA
// ---------------------------------------------------------------------------

std::vector<GpuStats> GpuMonitor::read_nvidia_linux() {
    std::vector<GpuStats> result;

    try {
        for (const auto& card : fs::directory_iterator("/sys/class/drm/")) {
            std::string cname = card.path().filename().string();
            if (cname.find("card") == std::string::npos || cname.find('-') != std::string::npos) continue;

            std::string dev_path = card.path().string() + "/device";
            auto vendor_id = utils::read_file(dev_path + "/vendor");
            if (!vendor_id.has_value()) continue;
            if (utils::trim(vendor_id.value()) != "0x10de") continue; // NVIDIA vendor ID

            GpuStats gs;
            gs.vendor = "NVIDIA";
            gs.name   = "NVIDIA GPU (" + cname + ")";

            // NVIDIA sysfs paths differ; try common ones
            auto gt_freq = utils::read_file(dev_path + "/gt_cur_freq_mhz");
            if (gt_freq.has_value()) {
                try { gs.frequency_mhz = std::stod(utils::trim(gt_freq.value())); } catch (...) {}
            }

            // hwmon for temperature
            try {
                for (const auto& hw : fs::directory_iterator(dev_path + "/hwmon/")) {
                    auto temp = utils::read_file(hw.path().string() + "/temp1_input");
                    if (temp.has_value()) {
                        try { gs.temperature_celsius = std::stod(utils::trim(temp.value())) / 1000.0; } catch (...) {}
                    }
                    break;
                }
            } catch (...) {}

            result.push_back(gs);
        }
    } catch (...) {}

    return result;
}

// ---------------------------------------------------------------------------
// Linux Intel (i915)
// ---------------------------------------------------------------------------

std::vector<GpuStats> GpuMonitor::read_intel_linux() {
    std::vector<GpuStats> result;

    try {
        for (const auto& card : fs::directory_iterator("/sys/class/drm/")) {
            std::string cname = card.path().filename().string();
            if (cname.find("card") == std::string::npos || cname.find('-') != std::string::npos) continue;

            std::string dev_path = card.path().string() + "/device";
            auto vendor_id = utils::read_file(dev_path + "/vendor");
            if (!vendor_id.has_value()) continue;
            if (utils::trim(vendor_id.value()) != "0x8086") continue; // Intel vendor ID

            GpuStats gs;
            gs.vendor      = "Intel";
            gs.name        = "Intel GPU (" + cname + ")";
            gs.memory_type = "Shared";

            // Frequency
            auto freq = utils::read_file(card.path().string() + "/gt_act_freq_mhz");
            if (!freq.has_value()) freq = utils::read_file(card.path().string() + "/gt_cur_freq_mhz");
            if (freq.has_value()) {
                try { gs.frequency_mhz = std::stod(utils::trim(freq.value())); } catch (...) {}
            }

            // hwmon
            try {
                for (const auto& hw : fs::directory_iterator(dev_path + "/hwmon/")) {
                    auto temp = utils::read_file(hw.path().string() + "/temp1_input");
                    if (temp.has_value()) {
                        try { gs.temperature_celsius = std::stod(utils::trim(temp.value())) / 1000.0; } catch (...) {}
                    }
                    break;
                }
            } catch (...) {}

            result.push_back(gs);
        }
    } catch (...) {}

    return result;
}

#else
std::vector<GpuStats> GpuMonitor::read_amd_linux()    { return {}; }
std::vector<GpuStats> GpuMonitor::read_nvidia_linux() { return {}; }
std::vector<GpuStats> GpuMonitor::read_intel_linux()  { return {}; }
#endif
