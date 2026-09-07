#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>

#ifndef SYSMON_BINARY_PATH
#  error "SYSMON_BINARY_PATH must be defined (see tests/CMakeLists.txt)"
#endif

namespace {

std::string run(const std::string& cmd, int& exit_code) {
    std::string out;
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
    if (!pipe) {
        exit_code = -1;
        return "";
    }
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        out += buffer.data();
    }
    exit_code = pclose(pipe);
    if (exit_code != -1) {
        exit_code = WEXITSTATUS(exit_code);
    }
    return out;
}

} // namespace

TEST(CliSmokeTest, VersionPrintsAndExitsZero) {
    int code = -1;
    std::string out = run(std::string(SYSMON_BINARY_PATH) + " --version", code);
    EXPECT_EQ(code, 0) << out;
    EXPECT_NE(out.find("sysmon"), std::string::npos) << out;
}

TEST(CliSmokeTest, HelpPrintsAndExitsZero) {
    int code = -1;
    std::string out = run(std::string(SYSMON_BINARY_PATH) + " --help", code);
    EXPECT_EQ(code, 0) << out;
    EXPECT_NE(out.find("Usage:"), std::string::npos) << out;
}

TEST(CliSmokeTest, OnceTerminatesWithoutTty) {
    int code = -1;
    std::string out = run(std::string(SYSMON_BINARY_PATH) + " --once", code);
    EXPECT_EQ(code, 0) << out;
    EXPECT_NE(out.find("System"), std::string::npos) << out;
}

TEST(CliSmokeTest, OnceNoTuiTerminatesWithoutTty) {
    int code = -1;
    std::string out = run(std::string(SYSMON_BINARY_PATH) + " --once --no-tui", code);
    EXPECT_EQ(code, 0) << out;
}

TEST(CliSmokeTest, ShowConfigPrintsAndExitsZero) {
    int code = -1;
    std::string out = run(std::string(SYSMON_BINARY_PATH) + " --show-config", code);
    EXPECT_EQ(code, 0) << out;
    EXPECT_NE(out.find("Config {"), std::string::npos) << out;
}
TEST(CliSmokeTest, JsonOutputIsProducedAndSelfContained) {
    int code = -1;
    const std::string out = run(std::string(SYSMON_BINARY_PATH) + " --json", code);
    EXPECT_EQ(code, 0) << out;
    EXPECT_EQ(out.front(), '{') << out.substr(0, 80);
    EXPECT_NE(out.find("\"sysmon_version\""), std::string::npos);
    EXPECT_NE(out.find("\"cpu\""), std::string::npos);
    EXPECT_NE(out.find("\"processes\""), std::string::npos);
    // JSON output must not be mixed with the human-readable renderer.
    EXPECT_EQ(out.find("Hostname "), std::string::npos) << out.substr(0, 200);
}

TEST(CliSmokeTest, JsonCompactIsASingleLine) {
    int code = -1;
    std::string out = run(std::string(SYSMON_BINARY_PATH) + " --json-compact", code);
    EXPECT_EQ(code, 0) << out;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    EXPECT_EQ(out.find('\n'), std::string::npos) << out.substr(0, 200);
}

TEST(CliSmokeTest, UnknownOptionIsRejectedInsteadOfIgnored) {
    // "--limt 5" used to be silently accepted and do nothing at all.
    int code = -1;
    const std::string out = run(std::string(SYSMON_BINARY_PATH) + " --limt 5 2>&1", code);
    EXPECT_NE(code, 0) << out;
    EXPECT_NE(out.find("unknown option"), std::string::npos) << out;
}

TEST(CliSmokeTest, InvalidValuesAreRejected) {
    int code = -1;
    std::string out = run(std::string(SYSMON_BINARY_PATH) + " --interval abc 2>&1", code);
    EXPECT_NE(code, 0) << out;

    out = run(std::string(SYSMON_BINARY_PATH) + " --sort bogus 2>&1", code);
    EXPECT_NE(code, 0) << out;
    EXPECT_NE(out.find("unknown sort key"), std::string::npos) << out;

    out = run(std::string(SYSMON_BINARY_PATH) + " --interval 2>&1", code);
    EXPECT_NE(code, 0) << out;
}

TEST(CliSmokeTest, SortKeysAreAccepted) {
    for (const char* key : {"cpu", "mem", "pid", "name", "time"}) {
        int code = -1;
        const std::string out =
            run(std::string(SYSMON_BINARY_PATH) + " --once --no-tui --limit 3 --sort " + key, code);
        EXPECT_EQ(code, 0) << key << ": " << out;
    }
}

TEST(CliSmokeTest, ConfigFlagDoesNotDiscardEarlierFlags) {
    // "--no-gpu --config PATH" used to reload the whole config mid-parse and
    // silently throw away --no-gpu.
    int code = -1;
    const std::string out = run(
        std::string(SYSMON_BINARY_PATH) + " --no-gpu --config /nonexistent-sysmon.conf --show-config",
        code);
    EXPECT_EQ(code, 0) << out;
    EXPECT_NE(out.find("Config {"), std::string::npos) << out;
}

namespace {

/// Count the process / connection objects in a --json-compact document.
///
/// Both are arrays of objects with a stable first key, so the opening brace
/// plus that key identifies an element without pulling in a JSON parser.
std::size_t count_occurrences(const std::string& haystack, const std::string& needle) {
    std::size_t n = 0;
    for (std::size_t pos = haystack.find(needle);
         pos != std::string::npos;
         pos = haystack.find(needle, pos + needle.size())) {
        ++n;
    }
    return n;
}

std::string write_temp_conf(const std::string& name, const std::string& contents) {
    const std::string dir = "/tmp/sysmon_cli_test_" + std::to_string(::getpid());
    ::system(("mkdir -p " + dir).c_str());
    const std::string path = dir + "/" + name;
    FILE* f = std::fopen(path.c_str(), "w");
    if (f) {
        std::fwrite(contents.data(), 1, contents.size(), f);
        std::fclose(f);
    }
    return path;
}

} // namespace

TEST(CliJsonExportTest, DisplayTogglesDoNotShapeTheExport) {
    // --json is a data export, not a view.  If the toggles shaped it,
    // "processes": [] would mean "none exist" on one machine and "the config
    // hid them" on another, and a stale show_gpu=false in ~/.config would
    // silently truncate every snapshot.
    int code = -1;
    const std::string out = run(
        std::string(SYSMON_BINARY_PATH) +
        " --json-compact --no-proc --no-net --no-conn --no-gpu"
        " --no-disk --no-temp --no-battery --no-cores", code);
    ASSERT_EQ(code, 0) << out;

    for (const char* section : {"processes", "connections", "network", "gpus",
                                "disks", "disk_io", "sensors", "battery",
                                "cpu", "memory", "system", "load"}) {
        const std::string key = std::string("\"") + section + "\":";
        ASSERT_NE(out.find(key), std::string::npos) << section << " missing";
        EXPECT_EQ(out.find(key + "[]"), std::string::npos)
            << section << " was emptied by a display toggle";
    }
}

TEST(CliJsonExportTest, ConfigLimitsDoNotTruncateTheExport) {
    // The list limits exist to fit a terminal.  Applied to --json they would
    // hand out a truncated array indistinguishable from a complete one, driven
    // by a config file the consumer of the JSON never sees.
    const std::string conf = write_temp_conf(
        "tiny.conf", "[display]\nproc_limit = 3\nconnections_limit = 4\n");

    int code = -1;
    const std::string capped = run(
        std::string(SYSMON_BINARY_PATH) + " --config " + conf + " --json-compact", code);
    ASSERT_EQ(code, 0) << capped;

    // More processes than the config's limit must still be present.
    EXPECT_GT(count_occurrences(capped, "{\"pid\":"), 3u)
        << "config proc_limit truncated the JSON export";
    EXPECT_GT(count_occurrences(capped, "{\"protocol\":"), 4u)
        << "config connections_limit truncated the JSON export";
}

TEST(CliJsonExportTest, ExplicitLimitIsStillHonoured) {
    // An explicit --limit is a deliberate request, unlike an inherited config
    // value, so it must keep working for JSON too.
    int code = -1;
    const std::string out =
        run(std::string(SYSMON_BINARY_PATH) + " --limit 2 --json-compact", code);
    ASSERT_EQ(code, 0) << out;
    EXPECT_EQ(count_occurrences(out, "{\"pid\":"), 2u) << out.substr(0, 200);
}

TEST(CliViewTest, DetailLevelsAreAccepted) {
    for (const char* level : {"compact", "normal", "detailed", "full"}) {
        int code = -1;
        const std::string out = run(
            std::string(SYSMON_BINARY_PATH) + " --once --no-tui --limit 3 --detail " + level, code);
        EXPECT_EQ(code, 0) << level << ": " << out;
        EXPECT_FALSE(out.empty()) << level;
    }
}

TEST(CliViewTest, ViewNamesAreAccepted) {
    for (const char* view : {"overview", "cpu", "memory", "gpu", "disk",
                             "network", "connections", "processes", "sensors"}) {
        int code = -1;
        const std::string out = run(
            std::string(SYSMON_BINARY_PATH) + " --once --no-tui --limit 3 --view " + view, code);
        EXPECT_EQ(code, 0) << view << ": " << out;
    }
}

TEST(CliViewTest, InvalidDetailAndViewAreRejected) {
    int code = -1;
    std::string out = run(std::string(SYSMON_BINARY_PATH) + " --detail enormous 2>&1", code);
    EXPECT_NE(code, 0) << out;
    EXPECT_NE(out.find("unknown detail level"), std::string::npos) << out;

    out = run(std::string(SYSMON_BINARY_PATH) + " --view kitchen-sink 2>&1", code);
    EXPECT_NE(code, 0) << out;
    EXPECT_NE(out.find("unknown view"), std::string::npos) << out;

    out = run(std::string(SYSMON_BINARY_PATH) + " --detail 2>&1", code);
    EXPECT_NE(code, 0) << out;
}

TEST(CliViewTest, CompactTextOutputIsActuallyCompact) {
    // --compact was accepted and then ignored in text mode, so the flag looked
    // as though it had worked while the output was byte-identical.
    int code = -1;
    const std::string full = run(
        std::string(SYSMON_BINARY_PATH) + " --once --no-tui --limit 5", code);
    ASSERT_EQ(code, 0) << full;
    const std::string compact = run(
        std::string(SYSMON_BINARY_PATH) + " --once --no-tui --compact", code);
    ASSERT_EQ(code, 0) << compact;

    EXPECT_LT(count_occurrences(compact, "\n"), count_occurrences(full, "\n"))
        << "--compact produced no fewer lines than the default output";
    EXPECT_LT(compact.size(), full.size());
}

TEST(CliViewTest, DetailedTextOutputAddsColumnsRatherThanRemovingThem) {
    int code = -1;
    const std::string normal = run(
        std::string(SYSMON_BINARY_PATH) + " --once --no-tui --limit 5", code);
    ASSERT_EQ(code, 0);
    const std::string detailed = run(
        std::string(SYSMON_BINARY_PATH) + " --once --no-tui --limit 5 --detail detailed", code);
    ASSERT_EQ(code, 0);

    EXPECT_EQ(normal.find("NICE"), std::string::npos) << "NICE leaked into the normal table";
    EXPECT_NE(detailed.find("NICE"), std::string::npos);
    EXPECT_NE(detailed.find("DISK R"), std::string::npos);
    EXPECT_GT(detailed.size(), normal.size());
}

TEST(CliViewTest, AllShowsEveryProcessAndConnection) {
    // --limit 0 used to mean "print nothing" in the display loops, so --all
    // produced an empty table.
    int code = -1;
    const std::string limited = run(
        std::string(SYSMON_BINARY_PATH) + " --once --no-tui --limit 5", code);
    ASSERT_EQ(code, 0);
    const std::string all = run(
        std::string(SYSMON_BINARY_PATH) + " --once --no-tui --all", code);
    ASSERT_EQ(code, 0) << all;

    EXPECT_NE(all.find("Processes (all "), std::string::npos)
        << "--all did not lift the process limit";
    EXPECT_GT(all.size(), limited.size());
}

TEST(CliViewTest, ZeroLimitMeansEverythingNotNothing) {
    int code = -1;
    const std::string out = run(
        std::string(SYSMON_BINARY_PATH) + " --once --no-tui --limit 0", code);
    ASSERT_EQ(code, 0) << out;
    EXPECT_NE(out.find("Processes (all "), std::string::npos) << out.substr(0, 300);
}
