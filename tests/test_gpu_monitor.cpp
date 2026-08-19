#include <gtest/gtest.h>
#include "sysmon/gpu_monitor.hpp"

TEST(GpuMonitorTest, ReadDoesNotCrash) {
    GpuMonitor mon;
    auto gpus = mon.read();
    // On systems with GPUs (like Apple Silicon or Linux with DRM), gpus is populated
    for (const auto& g : gpus) {
        EXPECT_FALSE(g.name.empty());
        EXPECT_FALSE(g.vendor.empty());
    }
}
