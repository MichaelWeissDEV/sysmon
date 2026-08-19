#include <gtest/gtest.h>
#include "sysmon/text_renderer.hpp"

#include <iostream>
#include <sstream>

namespace {

class CoutCapture {
public:
    CoutCapture() : old_buf_(std::cout.rdbuf()) {
        std::cout.rdbuf(oss_.rdbuf());
    }
    ~CoutCapture() {
        std::cout.rdbuf(old_buf_);
    }
    std::string str() const { return oss_.str(); }

private:
    std::streambuf* old_buf_;
    std::ostringstream oss_;
};

CpuStats make_cpu_with_unknown_values() {
    CpuStats cpu;
    cpu.model = "Test CPU";
    cpu.logical_cores = 8;
    cpu.physical_cores = 4;
    cpu.frequency_mhz = std::nullopt;
    cpu.max_frequency_mhz = std::nullopt;
    cpu.temperature_celsius = std::nullopt;
    return cpu;
}

GpuStats make_apple_gpu_unavailable() {
    GpuStats g;
    g.name = "Apple M-Test GPU";
    g.vendor = "Apple";
    g.memory_type = "Unified";
    g.memory_total_bytes = 128ULL * 1024 * 1024 * 1024;
    g.memory_used_bytes = std::nullopt;
    g.memory_free_bytes = std::nullopt;
    g.memory_usage_percent = std::nullopt;
    g.usage_percent = std::nullopt;
    g.frequency_mhz = std::nullopt;
    return g;
}

} // namespace

TEST(TextRendererTest, UnknownCpuValuesRenderedAsNA) {
    CoutCapture cap;
    Config cfg = Config::defaults();
    cfg.show_gpu = false;
    cfg.show_memory = false;
    cfg.show_temperature = false;
    cfg.show_disk = false;
    cfg.show_network = false;
    cfg.show_connections = false;
    cfg.show_processes = false;

    TextRenderer renderer;
    renderer.render(SystemStats{}, make_cpu_with_unknown_values(), MemoryStats{},
                    {}, LoadStats{}, {}, {}, {}, {}, {}, TemperatureStats{}, cfg);

    std::string out = cap.str();
    EXPECT_NE(out.find("Frequency     N/A"), std::string::npos) << out;
    EXPECT_NE(out.find("Temperature   N/A"), std::string::npos) << out;
}

TEST(TextRendererTest, AppleGpuShowsUnifiedCapacityNotFakeUsage) {
    CoutCapture cap;
    Config cfg = Config::defaults();
    cfg.show_cpu = false;
    cfg.show_memory = false;
    cfg.show_temperature = false;
    cfg.show_disk = false;
    cfg.show_network = false;
    cfg.show_connections = false;
    cfg.show_processes = false;

    TextRenderer renderer;
    renderer.render(SystemStats{}, CpuStats{}, MemoryStats{},
                    {make_apple_gpu_unavailable()}, LoadStats{},
                    {}, {}, {}, {}, {}, TemperatureStats{}, cfg);

    std::string out = cap.str();
    EXPECT_NE(out.find("Usage       N/A"), std::string::npos) << out;
    EXPECT_NE(out.find("System unified-memory capacity: 128.0 GB"),
              std::string::npos) << out;
    // A fabricated "GPU Memory Used" derived from VM statistics must not appear.
    EXPECT_EQ(out.find("GPU Memory"), std::string::npos) << out;
}

TEST(TextRendererTest, KnownCpuValuesRenderedNumerically) {
    CoutCapture cap;
    Config cfg = Config::defaults();
    cfg.show_gpu = false;
    cfg.show_memory = false;
    cfg.show_temperature = false;
    cfg.show_disk = false;
    cfg.show_network = false;
    cfg.show_connections = false;
    cfg.show_processes = false;

    CpuStats cpu = make_cpu_with_unknown_values();
    cpu.frequency_mhz = 3200.0;
    cpu.max_frequency_mhz = 3600.0;
    cpu.temperature_celsius = 45.5;

    TextRenderer renderer;
    renderer.render(SystemStats{}, cpu, MemoryStats{},
                    {}, LoadStats{}, {}, {}, {}, {}, {}, TemperatureStats{}, cfg);

    std::string out = cap.str();
    EXPECT_NE(out.find("Frequency     3200 MHz / 3600 MHz max"), std::string::npos) << out;
    EXPECT_NE(out.find("Temperature   45.5 °C"), std::string::npos) << out;
}