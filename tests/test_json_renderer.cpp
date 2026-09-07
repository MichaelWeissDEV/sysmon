#include <gtest/gtest.h>

#include "sysmon/json_renderer.hpp"

#include <sstream>
#include <string>

namespace {

/// A minimal structural check: balanced braces/brackets outside string
/// literals, and no stray commas before a closing token.  This catches the
/// mistakes a hand-rolled serialiser actually makes, without pulling in a JSON
/// parser dependency.
bool structurally_valid(const std::string& json) {
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    char last_significant = '\0';

    for (char c : json) {
        if (in_string) {
            if (escaped)            escaped = false;
            else if (c == '\\')     escaped = true;
            else if (c == '"')      in_string = false;
            continue;
        }
        switch (c) {
            case '"': in_string = true; break;
            case '{': case '[': ++depth; break;
            case '}': case ']':
                --depth;
                if (depth < 0) return false;
                if (last_significant == ',') return false;   // trailing comma
                break;
            default: break;
        }
        if (c != ' ' && c != '\n' && c != '\t') last_significant = c;
    }
    return depth == 0 && !in_string;
}

Snapshot make_snapshot() {
    Snapshot snap;
    snap.system.hostname = "test-host";
    snap.system.os       = "Test OS";
    snap.cpu.model         = "Test CPU";
    snap.cpu.logical_cores = 4;
    snap.cpu.flags         = {"sse2", "avx2"};
    snap.cpu.per_core.resize(2);
    snap.memory.ram_total_bytes = 1024;
    snap.disks.push_back(DiskStats{});
    snap.disks.back().mountpoint = "/";
    snap.network.push_back(NetworkStats{});
    snap.network.back().name = "eth0";
    snap.net_global.dns_servers = {"1.1.1.1", "8.8.8.8"};
    snap.processes.push_back(ProcessStats{});
    snap.processes.back().name = "init";
    snap.connections.push_back(NetConnectionStats{});
    return snap;
}

} // namespace

TEST(JsonRendererTest, ProducesStructurallyValidJson) {
    JsonRenderer renderer;
    const std::string json = renderer.to_string(make_snapshot(), Config::defaults());

    EXPECT_TRUE(structurally_valid(json)) << json;
    EXPECT_NE(json.find("\"hostname\": \"test-host\""), std::string::npos) << json;
}

TEST(JsonRendererTest, CompactModeIsAlsoValid) {
    JsonRenderer renderer;
    renderer.set_pretty(false);
    const std::string json = renderer.to_string(make_snapshot(), Config::defaults());

    EXPECT_TRUE(structurally_valid(json)) << json;
    EXPECT_EQ(json.find('\n'), json.size() - 1) << "compact output must be one line";
}

TEST(JsonRendererTest, ArraysHoldBareValuesNotKeyedPairs) {
    JsonRenderer renderer;
    const std::string json = renderer.to_string(make_snapshot(), Config::defaults());

    // A string array element must never be emitted as `"": "value"`.
    EXPECT_EQ(json.find("\"\": "), std::string::npos) << json;
    EXPECT_NE(json.find("\"sse2\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"1.1.1.1\""), std::string::npos) << json;
}

TEST(JsonRendererTest, UnmeasurableValuesBecomeNullNotZero) {
    Snapshot snap = make_snapshot();
    snap.cpu.frequency_mhz       = std::nullopt;
    snap.cpu.temperature_celsius = std::nullopt;

    JsonRenderer renderer;
    const std::string json = renderer.to_string(snap, Config::defaults());

    EXPECT_NE(json.find("\"frequency_mhz\": null"), std::string::npos) << json;
    EXPECT_NE(json.find("\"temperature_celsius\": null"), std::string::npos) << json;
    // A measured value is still a number.
    snap.cpu.frequency_mhz = 3200.0;
    const std::string with_value = renderer.to_string(snap, Config::defaults());
    EXPECT_NE(with_value.find("\"frequency_mhz\": 3200.0"), std::string::npos) << with_value;
}

TEST(JsonRendererTest, EscapesQuotesAndBackslashesInNames) {
    Snapshot snap = make_snapshot();
    snap.processes.back().name    = R"(weird "name" \ here)";
    snap.disks.back().mountpoint  = R"(C:\Program Files)";

    JsonRenderer renderer;
    const std::string json = renderer.to_string(snap, Config::defaults());

    EXPECT_TRUE(structurally_valid(json)) << json;
    EXPECT_NE(json.find(R"(weird \"name\" \\ here)"), std::string::npos) << json;
    EXPECT_NE(json.find(R"(C:\\Program Files)"), std::string::npos) << json;
}

TEST(JsonRendererTest, EmptySnapshotStillProducesValidDocument) {
    JsonRenderer renderer;
    const std::string json = renderer.to_string(Snapshot{}, Config::defaults());
    EXPECT_TRUE(structurally_valid(json)) << json;
    EXPECT_NE(json.find("\"processes\""), std::string::npos);
}
