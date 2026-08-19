#include <gtest/gtest.h>
#include "sysmon/net_connections_monitor.hpp"

TEST(NetConnectionsMonitorTest, ReadDoesNotCrash) {
    NetConnectionsMonitor mon;
    auto conns = mon.read(false, 20);
    for (const auto& c : conns) {
        EXPECT_FALSE(c.protocol.empty());
    }
}
