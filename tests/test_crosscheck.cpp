#include <gtest/gtest.h>

#include "sysmon/disk_monitor.hpp"
#include "sysmon/network_monitor.hpp"
#include "sysmon/platform.hpp"
#include "sysmon/process_monitor.hpp"
#include "sysmon/utils.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <array>
#include <sstream>
#include <string>

// Cross-checks against the operating system's own reporting tools.
//
// Every other test here exercises sysmon's parsing against a fixture, which
// says nothing about whether the interface it reads is the right one.  These
// compare a collected value with what the platform's own utility reports for
// the same quantity, which is what caught the bug they now guard: on macOS the
// `if_data` counters getifaddrs() hands out are 32 bits wide and wrap every
// 4 GB, so an interface that had received 52391874737 bytes was reported as
// 851857408 — correct modulo 2^32, twelve wraps in, and wrong by 98 %.

namespace {

/// Interface name -> (rx bytes, tx bytes, rx packets, tx packets).
using LinkTotals = std::map<std::string, std::array<uint64_t, 4>>;

#if defined(SYSMON_MACOS)

/// Parse the Link# rows of `netstat -ib`.
///
/// netstat repeats an interface once per configured address, all carrying the
/// same counters; the `<Link#N>` row is the canonical one.
LinkTotals parse_netstat_ib(const std::string& output) {
    LinkTotals result;
    std::istringstream iss(output);
    std::string line;
    std::getline(iss, line);   // header

    while (std::getline(iss, line)) {
        const auto f = utils::split_whitespace(line);
        if (f.size() < 11) continue;
        if (f[2].rfind("<Link#", 0) != 0) continue;

        const auto ipkts  = utils::to_int(f[4]);
        const auto ibytes = utils::to_int(f[6]);
        const auto opkts  = utils::to_int(f[7]);
        const auto obytes = utils::to_int(f[9]);
        if (!ipkts || !ibytes || !opkts || !obytes) continue;

        result[f[0]] = {static_cast<uint64_t>(*ibytes), static_cast<uint64_t>(*obytes),
                        static_cast<uint64_t>(*ipkts),  static_cast<uint64_t>(*opkts)};
    }
    return result;
}

#endif // SYSMON_MACOS

} // namespace

#if defined(SYSMON_MACOS)

TEST(CrossCheckTest, InterfaceTotalsMatchNetstat) {
    const auto netstat = utils::run_command("netstat -ib 2>/dev/null");
    if (!netstat.has_value() || netstat->empty()) {
        GTEST_SKIP() << "netstat produced no output";
    }
    const LinkTotals truth = parse_netstat_ib(*netstat);
    if (truth.empty()) GTEST_SKIP() << "no Link# rows in netstat output";

    NetworkMonitor monitor;
    const std::vector<NetworkStats> interfaces = monitor.read();
    ASSERT_FALSE(interfaces.empty());

    int compared = 0;
    for (const auto& iface : interfaces) {
        const auto it = truth.find(iface.name);
        if (it == truth.end()) continue;   // netstat lists no counters for it
        ++compared;

        // The two readings are moments apart on a live machine, so compare
        // with a tolerance — but one wide enough only for traffic, not for a
        // 32-bit wrap, which is off by whole multiples of 4 GB.
        const auto close_enough = [](uint64_t got, uint64_t want, const char* what,
                                     const std::string& name) {
            if (want == 0) {
                EXPECT_LE(got, 1u << 20) << name << " " << what
                                         << ": netstat says 0, sysmon says " << got;
                return;
            }
            const double drift = std::abs(static_cast<double>(got) - static_cast<double>(want)) /
                                 static_cast<double>(want) * 100.0;
            EXPECT_LT(drift, 1.0) << name << " " << what << ": sysmon " << got
                                  << " vs netstat " << want << " (" << drift << " % apart)";
        };

        close_enough(iface.rx_bytes_total,   it->second[0], "rx bytes",   iface.name);
        close_enough(iface.tx_bytes_total,   it->second[1], "tx bytes",   iface.name);
        close_enough(iface.rx_packets_total, it->second[2], "rx packets", iface.name);
        close_enough(iface.tx_packets_total, it->second[3], "tx packets", iface.name);
    }

    EXPECT_GT(compared, 0) << "no interface could be compared against netstat";
}

TEST(CrossCheckTest, InterfaceTotalsAreNotTruncatedToThirtyTwoBits) {
    // A machine that has moved more than 4 GB through an interface is the only
    // one where the truncation is visible, so the check is conditional — but on
    // such a machine it is unambiguous: a wrapped counter is congruent to the
    // real one modulo 2^32 while being far smaller.
    const auto netstat = utils::run_command("netstat -ib 2>/dev/null");
    if (!netstat.has_value()) GTEST_SKIP() << "netstat unavailable";
    const LinkTotals truth = parse_netstat_ib(*netstat);

    constexpr uint64_t k4G = 1ULL << 32;
    bool checked = false;
    for (const auto& [name, totals] : truth) {
        if (totals[0] < k4G && totals[1] < k4G) continue;
        checked = true;

        NetworkMonitor monitor;
        for (const auto& iface : monitor.read()) {
            if (iface.name != name) continue;
            EXPECT_GE(iface.rx_bytes_total, totals[0] > k4G ? k4G : 0u)
                << name << " rx total looks truncated to 32 bits";
            EXPECT_GE(iface.tx_bytes_total, totals[1] > k4G ? k4G : 0u)
                << name << " tx total looks truncated to 32 bits";
        }
    }
    if (!checked) {
        GTEST_SKIP() << "no interface here has passed 4 GB, so a wrap cannot be observed";
    }
}

#endif // SYSMON_MACOS

#if defined(SYSMON_POSIX)

TEST(CrossCheckTest, RootFilesystemSizeMatchesDf) {
    // Only total and available are compared.  "Used" is a different quantity
    // on macOS — df reports the volume's own usage while statfs reports the
    // whole APFS container's — and a test that papered over that with a
    // tolerance would be asserting nothing.
    const auto df = utils::run_command("df -k / 2>/dev/null | tail -1");
    if (!df.has_value() || df->empty()) GTEST_SKIP() << "df produced no output";

    const auto fields = utils::split_whitespace(*df);
    if (fields.size() < 4) GTEST_SKIP() << "unexpected df layout: " << *df;

    const auto total_kb = utils::to_int(fields[1]);
    const auto avail_kb = utils::to_int(fields[3]);
    if (!total_kb || !avail_kb) GTEST_SKIP() << "df numbers unparseable: " << *df;

    DiskMonitor monitor;
    const auto disks = monitor.read();
    const auto root = std::find_if(disks.begin(), disks.end(),
                                   [](const DiskStats& d) { return d.mountpoint == "/"; });
    ASSERT_NE(root, disks.end()) << "no root filesystem was collected";

    const auto within = [](uint64_t got, uint64_t want, double percent) {
        if (want == 0) return got == 0;
        const double drift = std::abs(static_cast<double>(got) - static_cast<double>(want)) /
                             static_cast<double>(want) * 100.0;
        return drift < percent;
    };

    EXPECT_TRUE(within(root->total_bytes, static_cast<uint64_t>(*total_kb) * 1024, 1.0))
        << "root total: sysmon " << root->total_bytes << " vs df "
        << static_cast<uint64_t>(*total_kb) * 1024;
    // Free space moves on a live machine, so this one gets more room.
    EXPECT_TRUE(within(root->available_bytes, static_cast<uint64_t>(*avail_kb) * 1024, 5.0))
        << "root available: sysmon " << root->available_bytes << " vs df "
        << static_cast<uint64_t>(*avail_kb) * 1024;
}

TEST(CrossCheckTest, ProcessCpuTimeMatchesPs) {
    // Accumulated CPU time is the metric that was 41x too low on Apple Silicon
    // until the mach timebase conversion landed, and ps is the ground truth
    // that showed it.
    ProcessMonitor monitor;
    auto processes = monitor.read(0, ProcSort::Time);
    ASSERT_FALSE(processes.empty());

    int compared = 0;
    for (const auto& p : processes) {
        if (p.cpu_time_seconds < 60.0) continue;   // seconds-level rounding dominates below this

        const auto out = utils::run_command(
            "ps -o time= -p " + std::to_string(p.pid) + " 2>/dev/null");
        if (!out.has_value()) continue;
        const std::string text = utils::trim(*out);
        if (text.empty()) continue;   // exited between the two reads

        // ps prints [[dd-]hh:]mm:ss.
        double seconds = 0.0;
        std::string rest = text;
        if (const auto dash = rest.find('-'); dash != std::string::npos) {
            if (const auto days = utils::to_int(rest.substr(0, dash))) {
                seconds += static_cast<double>(*days) * 86400.0;
            }
            rest = rest.substr(dash + 1);
        }
        std::vector<std::string> parts;
        for (const auto& part : utils::split(rest, ':')) parts.push_back(part);
        if (parts.empty() || parts.size() > 3) continue;
        for (const auto& part : parts) {
            const auto value = utils::to_double(part);
            if (!value) { seconds = -1.0; break; }
            seconds = seconds * 60.0 + *value;
        }
        if (seconds < 0.0) continue;

        ++compared;
        // A second of drift between the two samples is expected; a unit
        // conversion error is not.  10 % catches the latter and tolerates the
        // former on any process with a minute of CPU time.
        const double drift = std::abs(p.cpu_time_seconds - seconds) / seconds * 100.0;
        EXPECT_LT(drift, 10.0)
            << "pid " << p.pid << " (" << p.name << "): sysmon "
            << p.cpu_time_seconds << " s vs ps \"" << text << "\" = " << seconds << " s";

        if (compared >= 5) break;
    }

    if (compared == 0) {
        GTEST_SKIP() << "no process here has a minute of accumulated CPU time";
    }
}

#endif // SYSMON_POSIX

TEST(CrossCheckTest, PlaceholderSoTheSuiteIsNeverEmpty) {
    SUCCEED() << "platform-specific cross-checks are compiled in where available";
}
