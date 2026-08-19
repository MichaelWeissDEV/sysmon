#include <gtest/gtest.h>
#include "sysmon/net_connections_monitor.hpp"

// ---------------------------------------------------------------------------
// Fixture-based pure parser tests (macOS netstat output).
// ---------------------------------------------------------------------------

namespace {

const char* kTcpFixture =
    "Active Internet connections (including servers)\n"
    "Proto Recv-Q Send-Q  Local Address          Foreign Address        (state)   rxbytes  txbytes  rhiwat  shiwat  process:pid   state  options\n"
    "tcp4       0      0  192.168.0.4.49868      4.207.44.69.443        ESTABLISHED  15058  3998  131072  131072 Code Helper (Plu:14454  00102 00000008\n"
    "tcp6       0      0  2a02:810d:f613:2.49877 2001:4860:4841:4.443   ESTABLISHED  15064  2237  131072  131600      agy:8686  00102 00000008\n"
    "tcp4       0      0  127.0.0.1.49788        *.*                    LISTEN            0     0  131072  131072 Code Helper (Plu:14454  00100 00000106\n"
    "tcp6       0      0  2606:4700:440b::.443   *.*                    LISTEN            0     0  131072  131072  CloudKitd:278  00100 00000106\n";

const char* kUdpFixture =
    "Active Internet connections (including servers)\n"
    "Proto Recv-Q Send-Q  Local Address          Foreign Address        rxbytes  txbytes  rhiwat  shiwat  process:pid   state  options\n"
    "udp4       0      0  192.168.0.4.60775      *.*                        0     0  786896    9216 plugin-container:1895  00100 00000000\n"
    "udp6       0      0  *.65412                *.*                        0  1503  786896    9216       syslogd:371  00180 00000000\n";

} // namespace

TEST(MacosNetstatTcpParserTest, ParsesEstablishedAndListen) {
    auto conns = NetConnectionsMonitor::parse_macos_netstat_tcp(kTcpFixture, true);
    ASSERT_EQ(conns.size(), 4u);

    const auto& c0 = conns[0];
    EXPECT_EQ(c0.protocol, "tcp4");
    EXPECT_EQ(c0.local_addr, "192.168.0.4");
    EXPECT_EQ(c0.local_port, 49868);
    EXPECT_EQ(c0.remote_addr, "4.207.44.69");
    EXPECT_EQ(c0.remote_port, 443);
    EXPECT_EQ(c0.state, "ESTABLISHED");
    EXPECT_EQ(c0.pid, 14454);
    EXPECT_EQ(c0.process_name, "Code Helper (Plu");
    EXPECT_EQ(c0.rx_bytes, 15058ull);
    EXPECT_EQ(c0.tx_bytes, 3998ull);
}

TEST(MacosNetstatTcpParserTest, ParsesIpv6) {
    auto conns = NetConnectionsMonitor::parse_macos_netstat_tcp(kTcpFixture, true);
    const auto* c6 = &conns[1];
    EXPECT_EQ(c6->protocol, "tcp6");
    EXPECT_EQ(c6->local_addr, "2a02:810d:f613:2");
    EXPECT_EQ(c6->local_port, 49877);
    EXPECT_EQ(c6->state, "ESTABLISHED");
    EXPECT_EQ(c6->pid, 8686);
    EXPECT_EQ(c6->process_name, "agy");
}

TEST(MacosNetstatTcpParserTest, ExcludesListenWhenRequested) {
    auto conns = NetConnectionsMonitor::parse_macos_netstat_tcp(kTcpFixture, false);
    ASSERT_EQ(conns.size(), 2u);
    for (const auto& c : conns) {
        EXPECT_NE(c.state, "LISTEN");
    }
}

TEST(MacosNetstatTcpParserTest, EmptyOutput) {
    auto conns = NetConnectionsMonitor::parse_macos_netstat_tcp("", true);
    EXPECT_TRUE(conns.empty());
}

TEST(MacosNetstatTcpParserTest, MalformedShortLinesIgnored) {
    std::string fixture = "Active Internet connections\n"
                          "Proto Recv-Q Send-Q\n"
                          "tcp4  0  0\n"
                          "udp4  0  0\n";
    auto conns = NetConnectionsMonitor::parse_macos_netstat_tcp(fixture, true);
    EXPECT_TRUE(conns.empty());
}

TEST(MacosNetstatTcpParserTest, UnexpectedColumnsTolerated) {
    // A line with extra columns still parses the known leading fields.
    auto conns = NetConnectionsMonitor::parse_macos_netstat_tcp(
        "Active Internet connections (including servers)\n"
        "Proto Recv-Q Send-Q  Local Address          Foreign Address        (state)   rxbytes  txbytes  rhiwat  shiwat  process:pid\n"
        "tcp4       0      0  10.0.0.1.80       10.0.0.2.443        ESTABLISHED  1  2  3  4  extra1 extra2 extra3 extra4\n",
        true);
    ASSERT_EQ(conns.size(), 1u);
    EXPECT_EQ(conns[0].local_addr, "10.0.0.1");
    EXPECT_EQ(conns[0].local_port, 80);
    EXPECT_EQ(conns[0].state, "ESTABLISHED");
    // No valid process:pid token -> N/A.
    EXPECT_EQ(conns[0].pid, -1);
    EXPECT_TRUE(conns[0].process_name.empty());
}

TEST(MacosNetstatTcpParserTest, WildcardRemote) {
    auto conns = NetConnectionsMonitor::parse_macos_netstat_tcp(kTcpFixture, true);
    const auto* listen = &conns[2];
    EXPECT_EQ(listen->state, "LISTEN");
    EXPECT_EQ(listen->remote_addr, "*.*");
    EXPECT_EQ(listen->remote_port, 0);
}

TEST(MacosNetstatUdpParserTest, ParsesUdpWithoutStateColumn) {
    auto conns = NetConnectionsMonitor::parse_macos_netstat_udp(kUdpFixture, true);
    ASSERT_EQ(conns.size(), 2u);

    const auto& c0 = conns[0];
    EXPECT_EQ(c0.protocol, "udp4");
    EXPECT_EQ(c0.local_addr, "192.168.0.4");
    EXPECT_EQ(c0.local_port, 60775);
    EXPECT_EQ(c0.remote_addr, "*.*");
    EXPECT_EQ(c0.state, "UNCONN");
    EXPECT_EQ(c0.pid, 1895);
    EXPECT_EQ(c0.process_name, "plugin-container");

    const auto& c1 = conns[1];
    EXPECT_EQ(c1.protocol, "udp6");
    EXPECT_EQ(c1.local_addr, "*");
    EXPECT_EQ(c1.local_port, 65412);
    EXPECT_EQ(c1.pid, 371);
}

TEST(MacosNetstatUdpParserTest, EmptyOutput) {
    auto conns = NetConnectionsMonitor::parse_macos_netstat_udp("", true);
    EXPECT_TRUE(conns.empty());
}

// ---------------------------------------------------------------------------
// Platform integration: reading real connections must not crash and every
// entry must carry a protocol.
// ---------------------------------------------------------------------------

TEST(NetConnectionsMonitorTest, ReadDoesNotCrash) {
    NetConnectionsMonitor mon;
    auto conns = mon.read(false, 20);
    for (const auto& c : conns) {
        EXPECT_FALSE(c.protocol.empty());
    }
}