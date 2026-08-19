#include <iostream>
#include "../include/sysmon/utils.hpp"
#include <cassert>

void test_format_bytes() {
    // Test basic formatting
    assert(utils::format_bytes(1023) == "1023 B");
    assert(utils::format_bytes(1024) == "1.0 KB");
    assert(utils::format_bytes(1024 * 1024) == "1.0 MB");
    assert(utils::format_bytes(1024 * 1024 * 1024) == "1.0 GB");

    // Test with values that should round to 0.5
    assert(utils::format_bytes(512 * 1024) == "0.5 MB");

    std::cout << "All format_bytes tests passed!" << std::endl;
}

void test_format_duration() {
    // Test uptime formatting
    assert(utils::format_duration("3600") == "1h 0m");  // 1 hour
    assert(utils::format_duration("7200") == "2h 0m");  // 2 hours
    assert(utils::format_duration("3661") == "1h 1m");  // 1 hour, 1 minute
    assert(utils::format_duration("86400") == "1d 0h 0m");  // 1 day

    std::cout << "All format_duration tests passed!" << std::endl;
}

int main() {
    try {
        test_format_bytes();
        test_format_duration();
        std::cout << "All utils tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}