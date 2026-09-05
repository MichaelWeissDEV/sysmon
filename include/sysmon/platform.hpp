/**
 * @file platform.hpp
 * @brief Platform detection macros and shared platform includes.
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

#if defined(SYSMON_LINUX) || defined(SYSMON_MACOS)
    #define SYSMON_POSIX 1
#endif

// ---------------------------------------------------------------------------
// Windows: keep <windows.h> lean and out of the way of the standard library
// ---------------------------------------------------------------------------

#if defined(SYSMON_WINDOWS)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX     // otherwise min/max macros break std::min/std::max
    #endif
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601   // Windows 7 and later
    #endif
#endif

/** @brief Human-readable name of the platform this binary was built for. */
#if defined(SYSMON_LINUX)
    #define SYSMON_PLATFORM_NAME "Linux"
#elif defined(SYSMON_MACOS)
    #define SYSMON_PLATFORM_NAME "macOS"
#elif defined(SYSMON_WINDOWS)
    #define SYSMON_PLATFORM_NAME "Windows"
#else
    #define SYSMON_PLATFORM_NAME "Unknown"
#endif

#endif // SYSMON_PLATFORM_HPP
