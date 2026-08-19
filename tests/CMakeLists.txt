cmake_minimum_required(VERSION 3.16)

# Find or fetch Google Test. When fetching, pin a concrete stable release;
# no floating branches. Fetching only happens when BUILD_TESTING=ON.
set(GTEST_VERSION_TAG "v1.14.0")

find_package(GTest QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        ${GTEST_VERSION_TAG}
        GIT_SHALLOW    TRUE
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
# Unit tests
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
sysmon_test(test_temperature_monitor   test_temperature_monitor.cpp)
sysmon_test(test_disk_io_monitor       test_disk_io_monitor.cpp)
sysmon_test(test_process_monitor       test_process_monitor.cpp)
sysmon_test(test_text_renderer         test_text_renderer.cpp)

# ---------------------------------------------------------------------------
# CLI smoke tests
# ---------------------------------------------------------------------------
add_executable(test_cli test_cli.cpp)
target_link_libraries(test_cli PRIVATE sysmon_lib GTest::gtest_main)
target_include_directories(test_cli PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(test_cli PRIVATE
    SYSMON_BINARY_PATH="${CMAKE_CURRENT_BINARY_DIR}/../sysmon")
gtest_discover_tests(test_cli)