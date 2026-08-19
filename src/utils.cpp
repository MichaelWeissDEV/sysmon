#include "sysmon/utils.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <iostream>

namespace utils {

std::optional<std::string> read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
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
    if (bps < 0) bps = 0;
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

std::string format_duration_seconds(double seconds) {
    if (seconds < 0) seconds = 0;
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
    try {
        double seconds = std::stod(uptime_seconds_str);
        return format_duration_seconds(seconds);
    } catch (...) {
        return uptime_seconds_str;
    }
}

} // namespace utils