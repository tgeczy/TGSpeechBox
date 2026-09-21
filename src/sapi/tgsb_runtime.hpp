/*
TGSpeechBox — SAPI runtime class (statically linked).
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

// Direct includes — statically linked, always available.
#include <espeak-ng/speak_lib.h>
#include <espeak-ng/espeak_ng.h>
#include "nvspFrontend.h"
#include "speechPlayer.h"

namespace TGSpeech {
namespace tgsb {

// ------------ Installation/path helpers ------------

std::wstring get_this_module_dir();
std::vector<std::wstring> get_installed_language_tags();
std::wstring get_language_display_name(const std::wstring& lang_tag);
std::wstring lang_tag_to_lcid_hex(const std::wstring& lang_tag);
std::vector<std::wstring> get_voice_profile_names();

// ------------ Runtime (synthesis pipeline) ------------

// Alias for the DSP sample type (struct wrapping a short).
using sample_t = sample;

struct speak_params {
    double speed = 1.0;
    double base_pitch = 110.0;
    double inflection = 0.55;
    char clause_type = '.';
    double volume = 1.0;
    std::wstring preset_name;
    int user_index_base = 0;
};

class runtime {
public:
    runtime();
    ~runtime();

    runtime(const runtime&) = delete;
    runtime& operator=(const runtime&) = delete;

    HRESULT ensure_initialized();
    HRESULT set_language(const std::wstring& lang_tag);
    HRESULT set_voice_profile(const std::wstring& profile_name);
    HRESULT queue_text(const std::wstring& text, const speak_params& params);
    int synthesize(int max_samples, sample* out_samples);
    void purge();

    // Samples the last queue_text() put on the DSP queue (the sum of the
    // frames' minimum lengths): a lower bound on the audio it will produce,
    // which places bookmarks while the clause is still streaming (#128).
    unsigned long long queued_samples() const noexcept { return queued_samples_; }

    int sample_rate() const noexcept { return sample_rate_; }
    const std::wstring& base_dir() const noexcept { return base_dir_; }
    void set_time_stretch(double factor);
    const std::wstring& current_voice_profile() const noexcept { return current_profile_name_; }

private:
    HRESULT init_espeak();
    void text_to_ipa_utf8(const std::wstring& text, std::string& out_ipa);
    void apply_preset_and_volume(void* frame, const speak_params& params);
    void apply_voicing_tone_if_available();

    // --- handles (opaque pointers returned by direct init calls) ---
    void* speech_player_ = nullptr;
    void* frontend_ = nullptr;
    bool espeak_initialized_ = false;

    // --- configuration/state ---
    int sample_rate_ = 22050;
    std::wstring module_dir_;
    std::wstring base_dir_;
    std::wstring espeak_data_dir_;
    std::wstring current_lang_tag_;
    std::wstring current_profile_name_;

    nvspFrontend_VoicingTone cached_voicing_tone_{};
    bool has_voicing_tone_ = false;
    unsigned long long queued_samples_ = 0;

    // Frame callbacks for nvspFrontend.
    static void __cdecl frontend_frame_cb(void* userData, const nvspFrontend_Frame* frameOrNull, double durationMs, double fadeMs, int userIndex);
    static void __cdecl frontend_frame_ex_cb(void* userData, const nvspFrontend_Frame* frameOrNull, const nvspFrontend_FrameEx* frameExOrNull, double durationMs, double fadeMs, int userIndex);

    // eSpeak voice state.
    std::string current_espeak_voice_;
    std::string resolved_espeak_identifier_;
    std::string ipa_buf_;

    // eSpeak is not thread-safe. Simple mutex for the static-linked case
    // (no cross-DLL sharing, just protects within this process).
    std::mutex espeak_guard_;
};

} // namespace tgsb
} // namespace TGSpeech
