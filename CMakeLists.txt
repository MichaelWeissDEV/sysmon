cmake_minimum_required(VERSION 3.16)
project(sysmon VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /WX-)
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wconversion -Wshadow -Wno-sign-conversion
    )
endif()

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
include_directories(include)

# ---------------------------------------------------------------------------
# Source files (library)
# ---------------------------------------------------------------------------
set(SYSMON_LIB_SOURCES
    src/system_monitor.cpp
    src/cpu_monitor.cpp
    src/memory_monitor.cpp
    src/gpu_monitor.cpp
    src/load_monitor.cpp
    src/disk_monitor.cpp
    src/disk_io_monitor.cpp
    src/temperature_monitor.cpp
    src/network_monitor.cpp
    src/net_connections_monitor.cpp
    src/process_monitor.cpp
    src/config.cpp
    src/tui.cpp
    src/text_renderer.cpp
    src/utils.cpp
)

set(SYSMON_HEADERS
    include/sysmon/platform.hpp
    include/sysmon/stats.hpp
    include/sysmon/config.hpp
    include/sysmon/system_monitor.hpp
    include/sysmon/cpu_monitor.hpp
    include/sysmon/memory_monitor.hpp
    include/sysmon/gpu_monitor.hpp
    include/sysmon/load_monitor.hpp
    include/sysmon/disk_monitor.hpp
    include/sysmon/disk_io_monitor.hpp
    include/sysmon/temperature_monitor.hpp
    include/sysmon/network_monitor.hpp
    include/sysmon/net_connections_monitor.hpp
    include/sysmon/process_monitor.hpp
    include/sysmon/tui.hpp
    include/sysmon/text_renderer.hpp
    include/sysmon/utils.hpp
)

# ---------------------------------------------------------------------------
# Static library
# ---------------------------------------------------------------------------
add_library(sysmon_lib STATIC ${SYSMON_LIB_SOURCES} ${SYSMON_HEADERS})
target_include_directories(sysmon_lib PUBLIC include)

# Platform-specific linkage
if(APPLE)
    target_link_libraries(sysmon_lib PUBLIC
        "-framework IOKit"
        "-framework CoreFoundation"
    )
endif()

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(sysmon src/main.cpp)
target_link_libraries(sysmon PRIVATE sysmon_lib)

# Link pthread
find_package(Threads REQUIRED)
target_link_libraries(sysmon_lib PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS sysmon RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/sysmon DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# ---------------------------------------------------------------------------
# Doxygen documentation
# ---------------------------------------------------------------------------
find_package(Doxygen OPTIONAL_COMPONENTS dot)
if(DOXYGEN_FOUND)
    set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/docs/doxygen)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_XML  YES)
    set(DOXYGEN_RECURSIVE YES)
    doxygen_add_docs(doxygen
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        COMMENT "Generating Doxygen API docs"
    )
endif()