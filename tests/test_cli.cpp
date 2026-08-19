#include <gtest/gtest.h>

#include <array>
#include <cstdio>
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