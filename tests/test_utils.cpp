#include <gtest/gtest.h>

#include <cmath>
#include <limits>
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
// ---------------------------------------------------------------------------
// Terminal-width-aware text handling
// ---------------------------------------------------------------------------

TEST(UtilsTest, DisplayWidthCountsColumnsNotBytes) {
    EXPECT_EQ(utils::display_width("abc"), 3u);
    EXPECT_EQ(utils::display_width(""), 0u);
    // Multi-byte but single-column.
    EXPECT_EQ(utils::display_width("°C"), 2u);
    EXPECT_EQ(utils::display_width("café"), 4u);
    EXPECT_EQ(utils::display_width("↓↑"), 2u);
    // Double-width CJK.
    EXPECT_EQ(utils::display_width("日本語"), 6u);
    // Combining marks add no columns.
    EXPECT_EQ(utils::display_width("e\xCC\x81"), 1u);
}

TEST(UtilsTest, TruncateNeverSplitsAMultiByteCharacter) {
    // "über" is 5 bytes but 4 columns; cutting at 3 must not leave half of "ü".
    const std::string cut = utils::truncate("über", 3);
    EXPECT_EQ(utils::display_width(cut), 3u);
    EXPECT_EQ(cut, "üb\xE2\x80\xA6");   // two characters plus the ellipsis marker

    // Cutting at 1 leaves no room for the marker, so it is dropped entirely.
    EXPECT_EQ(utils::truncate("über", 1), "ü");

    // A CJK string cut mid-character would corrupt the terminal.
    const std::string cjk = utils::truncate("日本語テスト", 5);
    EXPECT_LE(utils::display_width(cjk), 5u);
}

TEST(UtilsTest, TruncateLeavesShortStringsAlone) {
    EXPECT_EQ(utils::truncate("abc", 10), "abc");
    EXPECT_EQ(utils::truncate("abc", 3), "abc");
    EXPECT_EQ(utils::truncate("", 5), "");
    EXPECT_EQ(utils::truncate("abc", 0), "");
}

TEST(UtilsTest, FitAlwaysProducesExactlyTheRequestedWidth) {
    for (const char* text : {"", "a", "short", "a-very-long-mountpoint-name",
                             "日本語テスト", "café-münchen"}) {
        for (std::size_t width : {1u, 4u, 8u, 20u, 40u}) {
            EXPECT_EQ(utils::display_width(utils::fit(text, width)), width)
                << "text=" << text << " width=" << width;
            EXPECT_EQ(utils::display_width(utils::fit_right(text, width)), width)
                << "text=" << text << " width=" << width;
        }
    }
}

TEST(UtilsTest, ColumnAlwaysKeepsASeparatingSpace) {
    // A value exactly as wide as the column used to run straight into the next
    // one ("ContinuityCaptureAgentmichaelweiss").
    const std::string cell = utils::column("ContinuityCaptureAgent", 22);
    EXPECT_EQ(utils::display_width(cell), 22u);
    EXPECT_EQ(cell.back(), ' ');

    const std::string exact = utils::column("abcd", 4);
    EXPECT_EQ(exact.back(), ' ');
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

TEST(UtilsTest, JsonEscapeHandlesQuotesBackslashesAndControls) {
    EXPECT_EQ(utils::json_escape(R"(say "hi")"), R"(say \"hi\")");
    EXPECT_EQ(utils::json_escape("C:\\Windows"), "C:\\\\Windows");
    EXPECT_EQ(utils::json_escape("a\nb\tc"), "a\\nb\\tc");
    EXPECT_EQ(utils::json_escape(std::string("\x01")), "\\u0001");
    // Valid UTF-8 passes through untouched.
    EXPECT_EQ(utils::json_escape("°C"), "°C");
}

TEST(UtilsTest, JsonNumberMapsUnsetAndNonFiniteToNull) {
    EXPECT_EQ(utils::json_number(std::nullopt), "null");
    EXPECT_EQ(utils::json_number(std::optional<double>(std::nan(""))), "null");
    EXPECT_EQ(utils::json_number(std::optional<double>(
                  std::numeric_limits<double>::infinity())), "null");
    EXPECT_EQ(utils::json_number(std::optional<double>(1.5), 1), "1.5");
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

TEST(UtilsTest, FormatRateScalesAndSpacesSensibly) {
    EXPECT_EQ(utils::format_rate(46.0, "/s"), "46/s");
    EXPECT_EQ(utils::format_rate(1500.0, "/s"), "1.5 k/s");
    EXPECT_EQ(utils::format_rate(2'500'000.0, "/s"), "2.5 M/s");
    // Negative and non-finite rates clamp to zero rather than printing garbage.
    EXPECT_EQ(utils::format_rate(-5.0, "/s"), "0.0/s");
}

TEST(UtilsTest, SplitWhitespaceDropsEmptyTokens) {
    const auto parts = utils::split_whitespace("  a   b\tc  ");
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0], "a");
    EXPECT_EQ(parts[2], "c");
    EXPECT_TRUE(utils::split_whitespace("   ").empty());
}

TEST(UtilsTest, ToIntAndToDoubleRejectNonNumericText) {
    EXPECT_EQ(utils::to_int("42"), 42);
    EXPECT_FALSE(utils::to_int("abc").has_value());
    EXPECT_FALSE(utils::to_int("").has_value());
    EXPECT_EQ(utils::to_double(" 1.5 "), 1.5);
    EXPECT_FALSE(utils::to_double("n/a").has_value());
}
