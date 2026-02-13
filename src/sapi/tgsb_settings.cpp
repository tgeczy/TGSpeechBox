/*
TGSpeechBox — SAPI wrapper settings (INI file load/save).
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "tgsb_settings.hpp"

#include "debug_log.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <mutex>
#include <vector>

namespace TGSpeech::tgsb {
namespace {

inline bool file_exists(const std::wstring& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

inline FILETIME file_mtime_or_zero(const std::wstring& path)
{
    FILETIME ft{};
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        ft = data.ftLastWriteTime;
    }
    return ft;
}

inline std::wstring trim_copy(std::wstring s)
{
    auto is_space = [](wchar_t ch) { return std::iswspace(ch) != 0; };
    while (!s.empty() && is_space(s.front())) {
        s.erase(s.begin());
    }
    while (!s.empty() && is_space(s.back())) {
        s.pop_back();
    }
    return s;
}

inline std::vector<std::wstring> split_list(const std::wstring& s)
{
    std::vector<std::wstring> out;
    std::wstring cur;
    cur.reserve(s.size());

    auto flush = [&]() {
        std::wstring t = trim_copy(cur);
        if (!t.empty()) {
            out.push_back(std::move(t));
        }
        cur.clear();
    };

    for (wchar_t ch : s) {
        switch (ch) {
        case L',':
        case L';':
        case L'\n':
        case L'\r':
        case L'\t':
        case L' ':
            flush();
            break;
        default:
            cur.push_back(ch);
            break;
        }
    }
    flush();
    return out;
}

inline std::wstring join_path(const std::wstring& a, const std::wstring& b)
{
    if (a.empty()) {
        return b;
    }
    if (b.empty()) {
        return a;
    }
    std::wstring out = a;
    if (out.back() != L'\\' && out.back() != L'/') {
        out += L'\\';
    }
    out += b;
    return out;
}

} // namespace

std::wstring normalize_lang_tag(const std::wstring& tag)
{
    std::wstring t = trim_copy(tag);
    for (wchar_t& ch : t) {
        if (ch == L'_') {
            ch = L'-';
        }
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return t;
}

std::wstring get_user_settings_path()
{
    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return std::wstring();
    }

    std::wstring dir(buf);
    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') {
        dir += L'\\';
    }
    dir += L"TGSpeechSapi";

    // Best-effort; ignore errors (folder may already exist).
    (void)CreateDirectoryW(dir.c_str(), nullptr);

    return join_path(dir, L"settings.ini");
}

std::wstring resolve_settings_path(const std::wstring& base_dir)
{
    const std::wstring user_path = get_user_settings_path();
    if (!user_path.empty() && file_exists(user_path)) {
        return user_path;
    }

    const std::wstring sys_path = join_path(base_dir, L"settings.ini");
    if (file_exists(sys_path)) {
        return sys_path;
    }

    // Prefer user path for future writes.
    return user_path.empty() ? sys_path : user_path;
}

wrapper_settings load_settings(const std::wstring& base_dir)
{
    wrapper_settings out;

    const std::wstring ini_path = resolve_settings_path(base_dir);
    if (ini_path.empty()) {
        return out;
    }

    const int log_val = GetPrivateProfileIntW(L"General", L"logging", 0, ini_path.c_str());
    out.logging_enabled = (log_val != 0);

    wchar_t buf[4096] = {};
    GetPrivateProfileStringW(L"Languages", L"excluded", L"", buf, static_cast<DWORD>(std::size(buf)), ini_path.c_str());

    const std::wstring excluded_raw(buf);
    for (const auto& part : split_list(excluded_raw)) {
        out.excluded_lang_tags.insert(normalize_lang_tag(part));
    }

    return out;
}

const wrapper_settings& get_settings_cached(const std::wstring& base_dir)
{
    struct cache_state {
        std::wstring ini_path;
        FILETIME mtime{};
        wrapper_settings settings{};
        bool initialized = false;
    };

    static std::mutex m;
    static cache_state cache;

    std::lock_guard<std::mutex> lock(m);

    const std::wstring ini_path = resolve_settings_path(base_dir);
    const FILETIME mtime = ini_path.empty() ? FILETIME{} : file_mtime_or_zero(ini_path);

    const bool changed_path = (!cache.initialized || ini_path != cache.ini_path);
    const bool changed_time = (cache.initialized && CompareFileTime(&mtime, &cache.mtime) != 0);

    if (changed_path || changed_time) {
        cache.ini_path = ini_path;
        cache.mtime = mtime;
        cache.settings = load_settings(base_dir);
        cache.initialized = true;

        DebugLog::SetEnabled(cache.settings.logging_enabled);
    } else if (!cache.initialized) {
        // First call but no ini_path; still apply defaults.
        cache.settings = load_settings(base_dir);
        cache.initialized = true;
        DebugLog::SetEnabled(cache.settings.logging_enabled);
    }

    return cache.settings;
}

} // namespace TGSpeech::tgsb
