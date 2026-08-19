#include <gtest/gtest.h>
#include "sysmon/utils.hpp"

TEST(UtilsTest, FormatBytes) {
    EXPECT_EQ(utils::format_bytes(0), "0 B");
    EXPECT_EQ(utils::format_bytes(1023), "1023 B");
    EXPECT_EQ(utils::format_bytes(1024), "1.0 KB");
    EXPECT_EQ(utils::format_bytes(1024 * 1024), "1.0 MB");
    EXPECT_EQ(utils::format_bytes(1024ULL * 1024 * 1024), "1.0 GB");
    EXPECT_EQ(utils::format_bytes(512 * 1024), "512.0 KB");
}

TEST(UtilsTest, FormatBytesPerSec) {
    EXPECT_EQ(utils::format_bytes_per_sec(0.0), "0.0 B/s");
    EXPECT_EQ(utils::format_bytes_per_sec(1024.0), "1.0 KB/s");
    EXPECT_EQ(utils::format_bytes_per_sec(1024.0 * 1024.0), "1.0 MB/s");
    EXPECT_EQ(utils::format_bytes_per_sec(-10.0), "0.0 B/s");
}

TEST(UtilsTest, FormatDurationSeconds) {
    EXPECT_EQ(utils::format_duration_seconds(0), "0s");
    EXPECT_EQ(utils::format_duration_seconds(59), "59s");
    EXPECT_EQ(utils::format_duration_seconds(3600), "1h 0m 0s");
    EXPECT_EQ(utils::format_duration_seconds(3661), "1h 1m 1s");
    EXPECT_EQ(utils::format_duration_seconds(86400), "1d 0h 0m 0s");
    EXPECT_EQ(utils::format_duration_seconds(-5), "0s");
}

TEST(UtilsTest, FormatDurationString) {
    EXPECT_EQ(utils::format_duration("3600"), "1h 0m 0s");
    EXPECT_EQ(utils::format_duration("not-a-number"), "not-a-number");
}

TEST(UtilsTest, Trim) {
    EXPECT_EQ(utils::trim("  hello \t"), "hello");
    EXPECT_EQ(utils::trim(""), "");
    EXPECT_EQ(utils::trim("   "), "");
    EXPECT_EQ(utils::trim("no-spaces"), "no-spaces");
}

TEST(UtilsTest, Split) {
    auto parts = utils::split("a b c", ' ');
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0], "a");
    EXPECT_EQ(parts[1], "b");
    EXPECT_EQ(parts[2], "c");

    auto csv = utils::split("1,2,3", ',');
    ASSERT_EQ(csv.size(), 3u);
    EXPECT_EQ(csv[0], "1");
}

TEST(UtilsTest, ReadFileMissing) {
    EXPECT_FALSE(utils::read_file("/nonexistent/path/that/does/not/exist").has_value());
}