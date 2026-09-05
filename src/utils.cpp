#include "sysmon/utils.hpp"
#include "sysmon/platform.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#if defined(SYSMON_WINDOWS)
#  define SYSMON_POPEN  _popen
#  define SYSMON_PCLOSE _pclose
#else
#  define SYSMON_POPEN  popen
#  define SYSMON_PCLOSE pclose
#endif

namespace utils {

// ---------------------------------------------------------------------------
// Files and strings
// ---------------------------------------------------------------------------

std::optional<std::string> read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

std::optional<std::string> read_first_line(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::string line;
    if (!std::getline(file, line)) {
        return std::nullopt;
    }
    return trim(line);
}

std::string trim(std::string_view str) {
    auto start = str.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string_view::npos) {
        return "";
    }
    auto end = str.find_last_not_of(" \t\n\r\f\v");
    return std::string(str.substr(start, end - start + 1));
}

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;
    while (std::getline(iss, token, delimiter)) {
        tokens.push_back(trim(token));
    }
    return tokens;
}

std::vector<std::string> split_whitespace(const std::string& str) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

bool starts_with(std::string_view str, std::string_view prefix) {
    return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
}

std::string to_lower(std::string_view str) {
    std::string out(str);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::optional<long long> to_int(std::string_view str) {
    std::string s = trim(str);
    if (s.empty()) return std::nullopt;
    try {
        std::size_t consumed = 0;
        long long value = std::stoll(s, &consumed);
        if (consumed == 0) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> to_double(std::string_view str) {
    std::string s = trim(str);
    if (s.empty()) return std::nullopt;
    try {
        std::size_t consumed = 0;
        double value = std::stod(s, &consumed);
        if (consumed == 0 || !std::isfinite(value)) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// Terminal-width-aware text handling
// ---------------------------------------------------------------------------
namespace {

/// Decode one UTF-8 sequence starting at @p i; advances @p i past it.
/// Invalid bytes decode as U+FFFD and consume exactly one byte, so a malformed
/// string still terminates and still produces sane column counts.
uint32_t decode_utf8(std::string_view s, std::size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t extra = 0;
    uint32_t cp = 0;

    if (c < 0x80)        { ++i; return c; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; extra = 3; }
    else                 { ++i; return 0xFFFD; }

    if (i + extra >= s.size()) {   // truncated sequence at end of string
        ++i;
        return 0xFFFD;
    }
    for (std::size_t k = 1; k <= extra; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) { ++i; return 0xFFFD; }
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    i += extra + 1;
    return cp;
}

/// Columns occupied by a single codepoint: 0 for combining marks, 2 for the
/// wide ranges (CJK, Hangul, emoji), 1 otherwise.
int codepoint_width(uint32_t cp) {
    if (cp == 0) return 0;
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0;   // control characters

    // Combining diacritical marks and other zero-width ranges.
    if ((cp >= 0x0300 && cp <= 0x036F) ||
        (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE00 && cp <= 0xFE0F) ||   // variation selectors
        (cp >= 0xFE20 && cp <= 0xFE2F) ||
        cp == 0x200B || cp == 0x200C || cp == 0x200D) {
        return 0;
    }

    if ((cp >= 0x1100 && cp <= 0x115F) ||   // Hangul Jamo
        (cp >= 0x2E80 && cp <= 0xA4CF) ||   // CJK radicals … Yi
        (cp >= 0xAC00 && cp <= 0xD7A3) ||   // Hangul syllables
        (cp >= 0xF900 && cp <= 0xFAFF) ||   // CJK compatibility ideographs
        (cp >= 0xFE30 && cp <= 0xFE6F) ||   // CJK compatibility forms
        (cp >= 0xFF00 && cp <= 0xFF60) ||   // fullwidth forms
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1F64F) || // emoji
        (cp >= 0x1F900 && cp <= 0x1F9FF) ||
        (cp >= 0x20000 && cp <= 0x3FFFD)) {
        return 2;
    }
    return 1;
}

} // namespace

std::size_t display_width(std::string_view str) {
    std::size_t width = 0;
    std::size_t i = 0;
    while (i < str.size()) {
        width += static_cast<std::size_t>(codepoint_width(decode_utf8(str, i)));
    }
    return width;
}

std::string truncate(std::string_view str, std::size_t width) {
    if (width == 0) return "";
    if (display_width(str) <= width) return std::string(str);

    // Leave one column for the ellipsis marker when there is room for it.
    const std::size_t budget = (width >= 2) ? width - 1 : width;
    const bool mark = (width >= 2);

    std::size_t used = 0;
    std::size_t i = 0;
    std::size_t cut = 0;
    while (i < str.size()) {
        std::size_t next = i;
        const int w = codepoint_width(decode_utf8(str, next));
        if (used + static_cast<std::size_t>(w) > budget) break;
        used += static_cast<std::size_t>(w);
        i = next;
        cut = i;
    }

    std::string out(str.substr(0, cut));
    if (mark) {
        out += "\xE2\x80\xA6"; // U+2026 HORIZONTAL ELLIPSIS
    }
    return out;
}

std::string fit(std::string_view str, std::size_t width) {
    std::string out = truncate(str, width);
    const std::size_t w = display_width(out);
    if (w < width) out.append(width - w, ' ');
    return out;
}

std::string fit_right(std::string_view str, std::size_t width) {
    std::string out = truncate(str, width);
    const std::size_t w = display_width(out);
    if (w < width) out.insert(0, width - w, ' ');
    return out;
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

std::string format_bytes(uint64_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int idx = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024.0 && idx < 5) {
        size /= 1024.0;
        ++idx;
    }
    if (idx == 0) {
        return std::to_string(bytes) + " B";
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << size << " " << units[idx];
    return oss.str();
}

std::string format_bytes_per_sec(double bps) {
    if (!std::isfinite(bps) || bps < 0) bps = 0;
    static const char* units[] = {"B/s", "KB/s", "MB/s", "GB/s"};
    int idx = 0;
    while (bps >= 1024.0 && idx < 3) {
        bps /= 1024.0;
        ++idx;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << bps << " " << units[idx];
    return oss.str();
}

std::string format_rate(double per_sec, const std::string& unit) {
    if (!std::isfinite(per_sec) || per_sec < 0) per_sec = 0;
    std::ostringstream oss;
    if (per_sec >= 1'000'000.0) {
        oss << std::fixed << std::setprecision(1) << per_sec / 1'000'000.0 << " M" << unit;
    } else if (per_sec >= 1000.0) {
        oss << std::fixed << std::setprecision(1) << per_sec / 1000.0 << " k" << unit;
    } else {
        oss << std::fixed << std::setprecision(per_sec < 10.0 ? 1 : 0) << per_sec << " " << unit;
    }
    return oss.str();
}

std::string format_duration_seconds(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    long long s = static_cast<long long>(seconds);
    long long days    = s / 86400;
    long long hours   = (s % 86400) / 3600;
    long long minutes = (s % 3600) / 60;
    long long secs    = s % 60;

    std::ostringstream oss;
    if (days > 0) {
        oss << days << "d ";
    }
    if (hours > 0 || days > 0) {
        oss << hours << "h ";
    }
    if (minutes > 0 || hours > 0 || days > 0) {
        oss << minutes << "m ";
    }
    oss << secs << "s";
    return oss.str();
}

std::string format_duration(const std::string& uptime_seconds_str) {
    auto seconds = to_double(uptime_seconds_str);
    if (!seconds.has_value()) return uptime_seconds_str;
    return format_duration_seconds(seconds.value());
}

std::string format_time(long long epoch_seconds) {
    if (epoch_seconds <= 0) return "";
    std::time_t t = static_cast<std::time_t>(epoch_seconds);
    std::tm tm_buf{};
#if defined(SYSMON_WINDOWS)
    if (localtime_s(&tm_buf, &t) != 0) return "";
#else
    if (localtime_r(&t, &tm_buf) == nullptr) return "";
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf) == 0) return "";
    return buf;
}

std::string format_percent(const std::optional<double>& value) {
    if (!value.has_value() || !std::isfinite(value.value())) return "N/A";
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << value.value() << " %";
    return oss.str();
}

std::string json_escape(std::string_view str) {
    std::string out;
    out.reserve(str.size() + 8);
    for (unsigned char c : str) {
        switch (c) {
            case '"':  out += "\\\"";  break;
            case '\\': out += "\\\\";  break;
            case '\b': out += "\\b";   break;
            case '\f': out += "\\f";   break;
            case '\n': out += "\\n";   break;
            case '\r': out += "\\r";   break;
            case '\t': out += "\\t";   break;
            default:
                if (c < 0x20) {
                    // Control characters must be escaped as \u00XX.
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string json_number(const std::optional<double>& value, int precision) {
    if (!value.has_value() || !std::isfinite(value.value())) return "null";
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value.value();
    return oss.str();
}

// ---------------------------------------------------------------------------
// Process execution
// ---------------------------------------------------------------------------

std::optional<std::string> run_command(const std::string& command) {
#if defined(SYSMON_WINDOWS)
    const std::string cmd = command + " 2>NUL";
#else
    const std::string cmd = command + " 2>/dev/null";
#endif
    FILE* pipe = SYSMON_POPEN(cmd.c_str(), "r");
    if (pipe == nullptr) return std::nullopt;

    std::string output;
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    SYSMON_PCLOSE(pipe);
    return output;
}

} // namespace utils
