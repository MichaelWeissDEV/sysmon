#include <gtest/gtest.h>

#include <algorithm>
#include <vector>
#include "sysmon/process_monitor.hpp"

TEST(ProcessMonitorTest, FindsAtLeastOneProcess) {
    ProcessMonitor mon;
    auto procs = mon.read(100);
    EXPECT_FALSE(procs.empty());
    for (const auto& p : procs) {
        EXPECT_GT(p.pid, 0);
        EXPECT_FALSE(p.name.empty());
    }
}

TEST(ProcessMonitorTest, TwoSamplesProduceNoNegativeCpu) {
    ProcessMonitor mon;
    mon.read(100);
    auto second = mon.read(100);
    for (const auto& p : second) {
        EXPECT_GE(p.cpu_percent, 0.0);
    }
}

TEST(ProcessMonitorTest, MemoryPercentWithinRange) {
    ProcessMonitor mon;
    auto procs = mon.read(100);
    for (const auto& p : procs) {
        EXPECT_GE(p.mem_percent, 0.0);
        EXPECT_LE(p.mem_percent, 100.0);
    }
}
// ---------------------------------------------------------------------------
// Sorting
// ---------------------------------------------------------------------------

namespace {

std::vector<ProcessStats> make_process_list() {
    std::vector<ProcessStats> procs(3);
    procs[0].pid = 30; procs[0].name = "zebra"; procs[0].cpu_percent = 1.0;
    procs[0].mem_rss_bytes = 300; procs[0].cpu_time_seconds = 5.0;
    procs[1].pid = 10; procs[1].name = "alpha"; procs[1].cpu_percent = 9.0;
    procs[1].mem_rss_bytes = 100; procs[1].cpu_time_seconds = 50.0;
    procs[2].pid = 20; procs[2].name = "Middle"; procs[2].cpu_percent = 5.0;
    procs[2].mem_rss_bytes = 900; procs[2].cpu_time_seconds = 1.0;
    return procs;
}

} // namespace

TEST(ProcessMonitorTest, SortsByCpuDescending) {
    auto procs = make_process_list();
    ProcessMonitor::sort_processes(procs, ProcSort::Cpu);
    EXPECT_EQ(procs[0].pid, 10);
    EXPECT_EQ(procs[2].pid, 30);
}

TEST(ProcessMonitorTest, SortsByMemoryDescending) {
    auto procs = make_process_list();
    ProcessMonitor::sort_processes(procs, ProcSort::Memory);
    EXPECT_EQ(procs[0].mem_rss_bytes, 900u);
    EXPECT_EQ(procs[2].mem_rss_bytes, 100u);
}

TEST(ProcessMonitorTest, SortsByPidAscending) {
    auto procs = make_process_list();
    ProcessMonitor::sort_processes(procs, ProcSort::Pid);
    EXPECT_EQ(procs[0].pid, 10);
    EXPECT_EQ(procs[1].pid, 20);
    EXPECT_EQ(procs[2].pid, 30);
}

TEST(ProcessMonitorTest, SortsByNameCaseInsensitively) {
    auto procs = make_process_list();
    ProcessMonitor::sort_processes(procs, ProcSort::Name);
    EXPECT_EQ(procs[0].name, "alpha");
    EXPECT_EQ(procs[1].name, "Middle");
    EXPECT_EQ(procs[2].name, "zebra");
}

TEST(ProcessMonitorTest, SortsByAccumulatedCpuTime) {
    auto procs = make_process_list();
    ProcessMonitor::sort_processes(procs, ProcSort::Time);
    EXPECT_EQ(procs[0].pid, 10);
    EXPECT_EQ(procs[2].pid, 20);
}

TEST(ProcessMonitorTest, CpuTimeIsPlausibleForLongLivedProcesses) {
    // A macOS regression guard: task times are mach ticks, not nanoseconds, so
    // forgetting the timebase conversion understates CPU time by roughly 41x.
    // Something on a machine that has been up a while must have accumulated
    // more than a second of CPU.
    ProcessMonitor monitor;
    const auto procs = monitor.read(0, ProcSort::Time);
    ASSERT_FALSE(procs.empty());

    double max_cpu_time = 0.0;
    for (const auto& p : procs) max_cpu_time = std::max(max_cpu_time, p.cpu_time_seconds);
    EXPECT_GT(max_cpu_time, 1.0);
}
