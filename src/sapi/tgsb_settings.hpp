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

    // Sample rate (Hz). 0 = use default (22050).
    int sample_rate = 0;

    // Pause mode: 0 = off, 1 = short, 2 = long.
    int pauseMode = 1;

    // Pitch mode: "" = use language default, or one of:
    // "espeak_style", "fujisaki_style", "impulse_style", "klatt_style", "legacy"
    std::wstring pitchMode;

    // Pitch inflection scale: -1 = use language default, 0-100 maps to 0.0-2.0.
    int pitchInflectionScale = -1;

    // Voicing tone sliders (0-100, 50 = neutral for most, 0 = neutral for some).
    // -1 = not set (use default).
    int voiceTilt = -1;           // 50 = neutral
    int noiseGlottalMod = -1;     // 0 = neutral (off)
    int pitchSyncF1 = -1;         // 50 = neutral
    int pitchSyncB1 = -1;         // 50 = neutral
    int speedQuotient = -1;       // 50 = neutral
    int aspirationTilt = -1;      // 50 = neutral
    int cascadeBwScale = -1;      // 50 = neutral
    int voiceTremor = -1;         // 0 = neutral (off)
    int headSize = -1;            // 50 = neutral
    int chorusDepth = -1;         // 0 = off (default)
    int chorusDetune = -1;        // 33 = ~2.0 Hz (default)

    // FrameEx sliders (0-100).
    int frameExCreakiness = -1;   // 0 = off
    int frameExBreathiness = -1;  // 0 = off
    int frameExJitter = -1;       // 0 = off
    int frameExShimmer = -1;      // 0 = off
    int frameExSharpness = -1;    // 50 = neutral

    // Rate controls.
    bool rateBoostEnabled = false;    // doubles the rate; what is then above 2x is DSP time-stretch
    bool overrideSystemRate = false;  // ignore SAPI rate, use globalRate
    int globalRate = 50;              // 0-100 slider, maps to 0.3-4.0x
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
