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

/** @brief Read the entire content of a file. Returns nullopt on error. */
std::optional<std::string> read_file(const std::string& path);

/** @brief Strip leading and trailing whitespace. */
std::string trim(std::string_view str);

/** @brief Split a string by a single-character delimiter. */
std::vector<std::string> split(const std::string& str, char delimiter);

/** @brief Format a byte count as a human-readable string (e.g. "4.2 GB"). */
std::string format_bytes(uint64_t bytes);

/** @brief Format bytes/sec as human-readable (e.g. "12.3 MB/s"). */
std::string format_bytes_per_sec(double bytes_per_sec);

/** @brief Format uptime seconds as "2d 3h 15m". */
std::string format_duration(const std::string& uptime_seconds_str);

/** @brief Format uptime seconds as "2d 3h 15m" (double overload). */
std::string format_duration_seconds(double seconds);

} // namespace utils

#endif // SYSMON_UTILS_HPP