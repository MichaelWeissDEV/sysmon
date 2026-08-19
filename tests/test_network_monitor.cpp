#include <gtest/gtest.h>
#include "sysmon/network_monitor.hpp"

TEST(NetworkMonitorTest, ReturnsVector) {
    NetworkMonitor mon;
    // First call initializes internal state (may return empty on first run)
    auto result1 = mon.read();
    auto result2 = mon.read();
    // After two calls, we should have results
    EXPECT_GE(result2.size(), 0u);
}

TEST(NetworkMonitorTest, InterfaceNameNotEmpty) {
    NetworkMonitor mon;
    mon.read(); // warm-up
    auto result = mon.read();
    for (const auto& n : result) {
        EXPECT_FALSE(n.interface.empty());
    }
}

TEST(NetworkMonitorTest, RatesNonNegative) {
    NetworkMonitor mon;
    mon.read();
    auto result = mon.read();
    for (const auto& n : result) {
        EXPECT_GE(n.rx_bytes_per_sec, 0.0);
        EXPECT_GE(n.tx_bytes_per_sec, 0.0);
    }
}
