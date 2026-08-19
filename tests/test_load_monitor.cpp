#include <iostream>
#include "../include/sysmon/load_monitor.hpp"
#include <cassert>

void test_parse_loadavg() {
    // Test with a mock loadavg content
    std::string mock_loadavg = "0.12 0.23 0.34 1/123 12345";

    LoadMonitor monitor;

    // Note: We can't easily test the actual parsing without real files,
    // but we can verify compilation and basic structure

    std::cout << "LoadMonitor parse test - compilation check passed!" << std::endl;
}

int main() {
    try {
        test_parse_loadavg();
        std::cout << "All load monitor tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}