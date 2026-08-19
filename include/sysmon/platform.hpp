/**
 * @file platform.hpp
 * @brief Platform detection macros and cross-platform type aliases.
 */

#ifndef SYSMON_PLATFORM_HPP
#define SYSMON_PLATFORM_HPP

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------

#if defined(__linux__)
    #define SYSMON_LINUX 1
#elif defined(__APPLE__) && defined(__MACH__)
    #define SYSMON_MACOS 1
#elif defined(_WIN32) || defined(_WIN64)
    #define SYSMON_WINDOWS 1
#else
    #define SYSMON_UNKNOWN 1
#endif

// ---------------------------------------------------------------------------
// Common helpers
// ---------------------------------------------------------------------------

#if defined(SYSMON_LINUX) || defined(SYSMON_MACOS)
    #define SYSMON_POSIX 1
#endif

#endif // SYSMON_PLATFORM_HPP
