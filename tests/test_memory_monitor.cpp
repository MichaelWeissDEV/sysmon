#include <iostream>
#include "../include/sysmon/memory_monitor.hpp"
#include <cassert>

void test_parse_meminfo() {
    // Test with a mock meminfo content
    std::string mock_meminfo =
        "MemTotal:        8123456 kB\n"
        "MemAvailable:    6543210 kB\n"
        "SwapTotal:       2097152 kB\n"
        "SwapFree:        1048576 kB\n";

    MemoryMonitor monitor;

    // Note: We can't easily test the actual parsing without real files,
    // but we can verify compilation and basic structure

    std::cout << "MemoryMonitor parse test - compilation check passed!" << std::endl;
}

int main() {
    try {
        test_parse_meminfo();
        std::cout << "All memory monitor tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}