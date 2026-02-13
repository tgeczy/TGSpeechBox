/*
TGSpeechBox — SAPI wrapper settings declarations.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

#include <string>
#include <unordered_set>

namespace TGSpeech::tgsb {

// Wrapper user-configurable settings.
//
// NOTE: The wrapper prefers per-user settings stored in %APPDATA%\TGSpeechSapi\settings.ini.
// If that file doesn't exist, it will fall back to {base_dir}\settings.ini (system-wide).
struct wrapper_settings {
    // Default OFF. Users can opt-in in the settings app.
    bool logging_enabled = false;

    // Normalized (lowercase, '-' separator) language tags that should be hidden.
    std::unordered_set<std::wstring> excluded_lang_tags;
};

// Normalizes a language tag for comparisons (trim, '_' -> '-', lowercase).
std::wstring normalize_lang_tag(const std::wstring& tag);

// Returns %APPDATA%\TGSpeechSapi\settings.ini (creating the TGSpeechSapi folder if needed).
// May return an empty string if APPDATA is not available.
std::wstring get_user_settings_path();

// Chooses which settings file should be used for reading:
// 1) user settings (if it exists)
// 2) {base_dir}\settings.ini (if it exists)
// 3) user settings path (even if it doesn't exist yet)
std::wstring resolve_settings_path(const std::wstring& base_dir);

// Loads settings from resolve_settings_path(base_dir). Missing values => defaults.
wrapper_settings load_settings(const std::wstring& base_dir);

// Cached settings with basic reload on file timestamp change.
// Also applies DebugLog::SetEnabled(settings.logging_enabled).
const wrapper_settings& get_settings_cached(const std::wstring& base_dir);

} // namespace TGSpeech::tgsb
