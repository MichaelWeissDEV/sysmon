#include <iostream>
#include "../include/sysmon/system_monitor.hpp"
#include <cassert>

// This test is mostly for compilation verification since we can't easily mock /etc/os-release
void test_system_monitor_compilation() {
    SystemMonitor monitor;
    // Just verify it compiles and can be instantiated
    auto stats = monitor.read();

    std::cout << "SystemMonitor compilation test passed!" << std::endl;
}

int main() {
    try {
        test_system_monitor_compilation();
        std::cout << "All system monitor tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}