cmake_minimum_required(VERSION 3.16)

# Find or fetch Google Test
find_package(GTest QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

# ---------------------------------------------------------------------------
# Helper macro
# ---------------------------------------------------------------------------
macro(sysmon_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE sysmon_lib GTest::gtest_main)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    gtest_discover_tests(${name})
endmacro()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
sysmon_test(test_utils                 test_utils.cpp)
sysmon_test(test_config                test_config.cpp)
sysmon_test(test_cpu_monitor           test_cpu_monitor.cpp)
sysmon_test(test_gpu_monitor           test_gpu_monitor.cpp)
sysmon_test(test_memory_monitor        test_memory_monitor.cpp)
sysmon_test(test_load_monitor          test_load_monitor.cpp)
sysmon_test(test_system_monitor        test_system_monitor.cpp)
sysmon_test(test_network_monitor       test_network_monitor.cpp)
sysmon_test(test_net_connections_monitor test_net_connections_monitor.cpp)
sysmon_test(test_disk_monitor          test_disk_monitor.cpp)