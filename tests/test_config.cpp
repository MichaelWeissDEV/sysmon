#include <gtest/gtest.h>
#include "sysmon/config.hpp"

TEST(ConfigTest, DefaultsValid) {
    auto cfg = Config::defaults();
    EXPECT_TRUE(cfg.show_cpu);
    EXPECT_TRUE(cfg.show_memory);
    EXPECT_TRUE(cfg.show_gpu);
    EXPECT_TRUE(cfg.show_network);
    EXPECT_GE(cfg.refresh_interval, 1);
}

TEST(ConfigTest, DisplayFlagsConversion) {
    auto cfg = Config::defaults();
    cfg.show_cpu = false;
    cfg.compact_mode = true;
    auto flags = cfg.to_display_flags();
    EXPECT_FALSE(flags.cpu);
    EXPECT_TRUE(flags.compact);
}
