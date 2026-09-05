/**
 * @file utils.hpp
 * @brief Shared utility functions for sysmon components.
 */

#ifndef SYSMON_UTILS_HPP
#define SYSMON_UTILS_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace utils {

// ---------------------------------------------------------------------------
// Files and strings
// ---------------------------------------------------------------------------

/** @brief Read the entire content of a file. Returns nullopt on error. */
std::optional<std::string> read_file(const std::string& path);

/** @brief Read the first line of a file, trimmed. Returns nullopt on error. */
std::optional<std::string> read_first_line(const std::string& path);

/** @brief Strip leading and trailing whitespace. */
std::string trim(std::string_view str);

/** @brief Split a string by a single-character delimiter (tokens are trimmed). */
std::vector<std::string> split(const std::string& str, char delimiter);

/** @brief Split on runs of whitespace; empty tokens are dropped. */
std::vector<std::string> split_whitespace(const std::string& str);

/** @brief True when @p str begins with @p prefix. */
bool starts_with(std::string_view str, std::string_view prefix);

/** @brief Lowercase copy (ASCII only). */
std::string to_lower(std::string_view str);

/** @brief Parse a decimal integer, returning nullopt when the text is not numeric. */
std::optional<long long> to_int(std::string_view str);

/** @brief Parse a floating point number, returning nullopt on failure. */
std::optional<double> to_double(std::string_view str);

// ---------------------------------------------------------------------------
// Terminal-width-aware text handling
// ---------------------------------------------------------------------------

/**
 * @brief Number of terminal columns a UTF-8 string occupies.
 *
 * Counts codepoints rather than bytes and gives East-Asian wide characters and
 * emoji a width of two.  Combining marks count as zero.
 */
std::size_t display_width(std::string_view str);

/**
 * @brief Truncate a UTF-8 string to at most @p width columns.
 *
 * Never cuts in the middle of a multi-byte sequence.  When truncation happens
 * and @p width is at least two, the last column becomes `…` so the reader can
 * see that the value was shortened.
 */
std::string truncate(std::string_view str, std::size_t width);

/**
 * @brief Truncate to @p width columns, then pad with spaces to exactly that width.
 *
 * This is the column primitive used by both renderers: unlike `std::setw` it
 * cannot overflow, so a long mountpoint or process name can never push the
 * following columns out of alignment.
 */
std::string fit(std::string_view str, std::size_t width);

/** @brief Right-align @p str within @p width columns (truncating if needed). */
std::string fit_right(std::string_view str, std::size_t width);

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

/** @brief Format a byte count as a human-readable string (e.g. "4.2 GB"). */
std::string format_bytes(uint64_t bytes);

/** @brief Format bytes/sec as human-readable (e.g. "12.3 MB/s"). */
std::string format_bytes_per_sec(double bytes_per_sec);

/** @brief Format a count/sec with a unit suffix (e.g. "1.2 k/s"). */
std::string format_rate(double per_sec, const std::string& unit);

/** @brief Format uptime seconds as "2d 3h 15m". */
std::string format_duration(const std::string& uptime_seconds_str);

/** @brief Format uptime seconds as "2d 3h 15m" (double overload). */
std::string format_duration_seconds(double seconds);

/** @brief Format a Unix epoch time as local "YYYY-MM-DD HH:MM:SS". */
std::string format_time(long long epoch_seconds);

/** @brief Format a percentage with one decimal, or "N/A" when unset. */
std::string format_percent(const std::optional<double>& value);

/** @brief Escape a string for inclusion in a JSON document (without quotes). */
std::string json_escape(std::string_view str);

/**
 * @brief Render a double as a JSON number, or `null` when unset or non-finite.
 *
 * NaN and infinity are not valid JSON, so they degrade to `null` rather than
 * producing a document no parser will accept.
 */
std::string json_number(const std::optional<double>& value, int precision = 2);

// ---------------------------------------------------------------------------
// Process execution
// ---------------------------------------------------------------------------

/**
 * @brief Run a command and capture its stdout.
 *
 * Returns nullopt when the command cannot be started.  stderr is discarded.
 */
std::optional<std::string> run_command(const std::string& command);

} // namespace utils

#endif // SYSMON_UTILS_HPP
