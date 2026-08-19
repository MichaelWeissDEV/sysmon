#include <iostream>
#include "../include/sysmon/cpu_monitor.hpp"
#include <cassert>

void test_cpu_monitor_compilation() {
    CpuMonitor monitor;
    // Just verify it compiles and can be instantiated
    auto stats = monitor.read();

    std::cout << "CpuMonitor compilation test passed!" << std::endl;
}

int main() {
    try {
        test_cpu_monitor_compilation();
        std::cout << "All CPU monitor tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}