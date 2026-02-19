/*
TGSpeechBox — Compile-time debug logging macros for SAPI engine.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

// Compile-time enable/disable. When enabled, logging can still be turned off at
// runtime via DebugLog::SetEnabled(false).
#define ENABLE_DEBUG_LOG 0

#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace DebugLog {

inline std::atomic<bool>& enabled_flag()
{
    // Default OFF. Users can opt-in via the settings app / settings.ini.
    static std::atomic<bool> enabled{false};
    return enabled;
}

inline void SetEnabled(bool enabled)
{
    enabled_flag().store(enabled, std::memory_order_relaxed);
}

inline bool IsEnabled()
{
    return enabled_flag().load(std::memory_order_relaxed);
}

inline std::wstring GetLogPath()
{
    wchar_t tempPath[MAX_PATH] = {};
    const DWORD len = ::GetTempPathW(MAX_PATH, tempPath);
    if (len == 0 || len >= MAX_PATH) {
        return L"TGSpeechSapi_debug.log";
    }

    std::wstring path(tempPath);
    if (!path.empty() && path.back() != L'\\') {
        path += L'\\';
    }
    path += L"TGSpeechSapi_debug.log";
    return path;
}

inline void TruncateIfTooLarge(const std::wstring& path)
{
    // Keep the log size bounded to avoid unbounded growth if a user turns
    // logging on and forgets about it.
    constexpr unsigned long long kMaxBytes = 1024ull * 1024ull; // 1 MiB

    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return;
    }

    ULARGE_INTEGER sz;
    sz.HighPart = data.nFileSizeHigh;
    sz.LowPart = data.nFileSizeLow;
    if (sz.QuadPart <= kMaxBytes) {
        return;
    }

    // Truncate.
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") == 0 && f) {
        fclose(f);
    }
}

inline void Log(const char* fmt, ...)
{
#if ENABLE_DEBUG_LOG
    if (!IsEnabled()) {
        return;
    }

    FILE* f = nullptr;
    const std::wstring path = GetLogPath();
    TruncateIfTooLarge(path);
    if (_wfopen_s(&f, path.c_str(), L"a+, ccs=UTF-8") != 0 || !f) {
        return;
    }

    SYSTEMTIME st;
    ::GetLocalTime(&st);

    std::fprintf(f,
                 "[%04d-%02d-%02d %02d:%02d:%02d] ",
                 st.wYear,
                 st.wMonth,
                 st.wDay,
                 st.wHour,
                 st.wMinute,
                 st.wSecond);

    va_list args;
    va_start(args, fmt);
    std::vfprintf(f, fmt, args);
    va_end(args);

    std::fprintf(f, "\n");
    std::fclose(f);
#else
    (void)fmt;
#endif
}

inline void ClearLog()
{
#if ENABLE_DEBUG_LOG
    if (!IsEnabled()) {
        return;
    }

    FILE* f = nullptr;
    const std::wstring path = GetLogPath();
    if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") == 0 && f) {
        std::fclose(f);
    }
#endif
}

} // namespace DebugLog

#if ENABLE_DEBUG_LOG
#define DEBUG_LOG(...) DebugLog::Log(__VA_ARGS__)
#else
#define DEBUG_LOG(...) (void)0
#endif
