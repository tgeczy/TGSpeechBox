/*
TGSpeechBox — SAPI runtime class and function pointer definitions.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

#include <cstdint>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

namespace TGSpeech {
namespace tgsb {

// ------------ Installation/path helpers ------------

// Directory where this TGSpeechSapi DLL is loaded from (no trailing slash).
std::wstring get_this_module_dir();

// Returns language tags based on installed packs (packs/lang/*.yaml). If not found, returns a
// small fallback list.
std::vector<std::wstring> get_installed_language_tags();

// Human-friendly name for a language tag (best effort; may return the tag itself).
std::wstring get_language_display_name(const std::wstring& lang_tag);

// LCID as uppercase hex string (e.g. "409"). Best effort; defaults to "409".
std::wstring lang_tag_to_lcid_hex(const std::wstring& lang_tag);

// Get list of voice profile names from the frontend (ABI v2+).
// Creates a temporary frontend handle to query, returns empty vector on failure.
std::vector<std::wstring> get_voice_profile_names();

// ------------ FrameEx parameters (DSP v5+ / Frontend ABI v2+) ------------

struct FrameEx {
    // Voice quality (DSP v5)
    double creakiness;      // laryngealization / creaky voice (0.0-1.0)
    double breathiness;     // breath noise mixed into voicing (0.0-1.0)
    double jitter;          // pitch period variation (0.0-1.0)
    double shimmer;         // amplitude variation (0.0-1.0)
    double sharpness;       // glottal closure sharpness multiplier (1.0 = neutral)
    // Formant end targets (DECTalk-style within-frame ramping, NAN = no ramp)
    double endCf1;          // Cascade F1 end target (Hz)
    double endCf2;          // Cascade F2 end target (Hz)
    double endCf3;          // Cascade F3 end target (Hz)
    double endPf1;          // Parallel F1 end target (Hz)
    double endPf2;          // Parallel F2 end target (Hz)
    double endPf3;          // Parallel F3 end target (Hz)
    // Fujisaki pitch model (DSP v6+, time units in samples)
    double fujisakiEnabled;     // 0.0 = off, >0.5 = on
    double fujisakiReset;       // rising edge resets model state
    double fujisakiPhraseAmp;   // phrase command amplitude (e.g. 1.3)
    double fujisakiPhraseLen;   // phrase filter L (samples). 0 = use default
    double fujisakiAccentAmp;   // accent command amplitude (e.g. 0.4)
    double fujisakiAccentDur;   // accent duration D (samples). 0 = use default
    double fujisakiAccentLen;   // accent filter L (samples). 0 = use default
    // Per-parameter transition speed scales (< 1.0 = reach target early, then hold)
    double transF1Scale;        // cf1, pf1, cb1, pb1
    double transF2Scale;        // cf2, pf2, cb2, pb2
    double transF3Scale;        // cf3, pf3, cb3, pb3
    double transNasalScale;     // cfN0, cfNP, cbN0, cbNP, caNP
    // Amplitude crossfade mode: 0.0 = linear, 1.0 = equal-power
    double transAmplitudeMode;
};

// ------------ VoicingTone parameters (DSP v5+ / Frontend ABI v2+) ------------

struct VoicingTone {
    double voicingPeakPos;        // Glottal pulse peak position (0.0-1.0)
    double voicedPreEmphA;        // Pre-emphasis coefficient A
    double voicedPreEmphMix;      // Pre-emphasis mix (0.0-1.0)
    double highShelfGainDb;       // High shelf EQ gain in dB
    double highShelfFcHz;         // High shelf EQ center frequency
    double highShelfQ;            // High shelf EQ Q factor
    double voicedTiltDbPerOct;    // Spectral tilt in dB/octave
    double noiseGlottalModDepth;  // Noise modulation by glottal cycle
    double pitchSyncF1DeltaHz;    // Pitch-synchronous F1 delta
    double pitchSyncB1DeltaHz;    // Pitch-synchronous B1 delta
    double speedQuotient;         // Glottal speed quotient (2.0 = neutral)
    double aspirationTiltDbPerOct; // Aspiration spectral tilt
    double cascadeBwScale;        // Global cascade bandwidth multiplier (1.0 = neutral)
    double tremorDepth;           // Tremor depth for elderly/shaky voice (0-0.5)
};

// ------------ Runtime (DLL loader + synthesis pipeline) ------------

// speechPlayer.dll uses 16-bit mono PCM.
using sample_t = int16_t;

struct speak_params {
    double speed = 1.0;       // 0.25..4.0 typically
    double base_pitch = 110.0; // Hz-ish
    double inflection = 0.55; // 0..1-ish
    char clause_type = '.';   // '.', ',', '?', '!'

    // Frame post-processing:
    double volume = 1.0; // 0..1
    std::wstring preset_name; // Voice profile name (was: "Adam" / "Benjamin" / ...)

    int user_index_base = 0;
};

class runtime {
public:
    runtime();
    ~runtime();

    runtime(const runtime&) = delete;
    runtime& operator=(const runtime&) = delete;

    // Ensure DLLs are loaded and handles created.
    // Returns S_OK on success, otherwise an HRESULT.
    HRESULT ensure_initialized();

    // Set language for both nvspFrontend and eSpeak.
    HRESULT set_language(const std::wstring& lang_tag);

    // Set the active voice profile (ABI v2+).
    // Profile names come from phonemes.yaml voiceProfiles section.
    HRESULT set_voice_profile(const std::wstring& profile_name);

    // Queue text for synthesis (text -> IPA via eSpeak -> frames via nvspFrontend -> speechPlayer queue).
    HRESULT queue_text(const std::wstring& text, const speak_params& params);

    // Synthesize up to `max_samples` into `out_samples`. Returns number of samples generated.
    int synthesize(int max_samples, sample_t* out_samples);

    // Purge any queued audio.
    void purge();

    // Sample rate used by speechPlayer.
    int sample_rate() const noexcept { return sample_rate_; }

    // Current voice profile name (empty if none set).
    const std::wstring& current_voice_profile() const noexcept { return current_profile_name_; }

private:
    // --- internal helpers ---
    HRESULT load_modules();
    HRESULT init_speech_player();
    HRESULT init_frontend();
    HRESULT init_espeak();

    // Convert text to IPA (UTF-8) using eSpeak-NG.
    // Writes into `out_ipa` and clears it on entry.
    void text_to_ipa_utf8(const std::wstring& text, std::string& out_ipa);
    void apply_preset_and_volume(void* frame /* points to struct of doubles */, const speak_params& params);

    // Apply VoicingTone from current voice profile to speechPlayer (ABI v2+).
    void apply_voicing_tone_if_available();

    // --- modules ---
    HMODULE speech_player_mod_ = nullptr;
    HMODULE frontend_mod_ = nullptr;
    HMODULE espeak_mod_ = nullptr;

    // --- handles ---
    void* speech_player_ = nullptr;
    void* frontend_ = nullptr;

    // --- configuration/state ---
    int sample_rate_ = 16000;
    // Directory containing TGSpeechSapi.dll.
    std::wstring module_dir_;

    // Directory containing shared runtime data like:
    //   <base_dir_>\\packs\\...
    //   <base_dir_>\\espeak-ng-data\\...
    //
    // Most installs are either:
    //  - single-folder (base_dir_ == module_dir_)
    //  - x86/x64 split (base_dir_ == parent(module_dir_))
    std::wstring base_dir_;

    // Resolved eSpeak-NG data dir, if present.
    std::wstring espeak_data_dir_;
    std::wstring current_lang_tag_;
    std::wstring current_profile_name_;  // Current voice profile (ABI v2+)

    // Cached VoicingTone from current profile; valid if has_voicing_tone_ is true.
    VoicingTone cached_voicing_tone_{};
    bool has_voicing_tone_ = false;

    // --- function pointers (speechPlayer.dll) ---
    using speechPlayer_initialize_t = void* (__cdecl*)(int sampleRate);
    using speechPlayer_queueFrame_t = void (__cdecl*)(void* player, void* framePtr, unsigned int minNumSamples, unsigned int numFadeSamples, int userIndex, bool purgeQueue);
    using speechPlayer_synthesize_t = int (__cdecl*)(void* player, unsigned int sampleCount, void* sampleBuf);
    using speechPlayer_getLastIndex_t = int (__cdecl*)(void* player);
    using speechPlayer_terminate_t = void (__cdecl*)(void* player);

    // Extended API (DSP v5+)
    using speechPlayer_queueFrameEx_t = void (__cdecl*)(void* player, void* framePtr, const void* frameExPtr, unsigned int frameExSize, unsigned int minNumSamples, unsigned int numFadeSamples, int userIndex, bool purgeQueue);
    using speechPlayer_setVoicingTone_t = void (__cdecl*)(void* player, const void* tone);
    using speechPlayer_getVoicingTone_t = void (__cdecl*)(void* player, void* tone);
    using speechPlayer_getDspVersion_t = unsigned int (__cdecl*)(void);

    speechPlayer_initialize_t speechPlayer_initialize_ = nullptr;
    speechPlayer_queueFrame_t speechPlayer_queueFrame_ = nullptr;
    speechPlayer_synthesize_t speechPlayer_synthesize_ = nullptr;
    speechPlayer_getLastIndex_t speechPlayer_getLastIndex_ = nullptr;
    speechPlayer_terminate_t speechPlayer_terminate_ = nullptr;

    // Extended (may be null on older DLLs)
    speechPlayer_queueFrameEx_t speechPlayer_queueFrameEx_ = nullptr;
    speechPlayer_setVoicingTone_t speechPlayer_setVoicingTone_ = nullptr;
    speechPlayer_getVoicingTone_t speechPlayer_getVoicingTone_ = nullptr;
    speechPlayer_getDspVersion_t speechPlayer_getDspVersion_ = nullptr;

    // --- function pointers (nvspFrontend.dll) ---
    struct nvsp_frame {
        double fields[47]; // must match nvspFrontend_Frame / speechPlayer frame layout
    };

    using nvspFrontend_create_t = void* (__cdecl*)(const char* packDirUtf8);
    using nvspFrontend_destroy_t = void (__cdecl*)(void* handle);
    using nvspFrontend_setLanguage_t = int (__cdecl*)(void* handle, const char* langTagUtf8);
    using nvspFrontend_frame_cb_t = void (__cdecl*)(void* userData, const void* frameOrNull, double durationMs, double fadeMs, int userIndex);
    using nvspFrontend_queueIPA_t = int (__cdecl*)(void* handle, const char* ipaUtf8, double speed, double basePitch, double inflection, const char* clauseTypeUtf8, int userIndexBase, nvspFrontend_frame_cb_t cb, void* userData);
    using nvspFrontend_getLastError_t = const char* (__cdecl*)(void* handle);

    // Extended API (ABI v2+)
    using nvspFrontend_frame_ex_cb_t = void (__cdecl*)(void* userData, const void* frameOrNull, const void* frameExOrNull, double durationMs, double fadeMs, int userIndex);
    using nvspFrontend_queueIPA_Ex_t = int (__cdecl*)(void* handle, const char* ipaUtf8, double speed, double basePitch, double inflection, const char* clauseTypeUtf8, int userIndexBase, nvspFrontend_frame_ex_cb_t cb, void* userData);
    using nvspFrontend_setVoiceProfile_t = int (__cdecl*)(void* handle, const char* profileNameUtf8);
    using nvspFrontend_getVoiceProfile_t = const char* (__cdecl*)(void* handle);
    using nvspFrontend_getVoiceProfileNames_t = const char* (__cdecl*)(void* handle);
    using nvspFrontend_getVoicingTone_t = int (__cdecl*)(void* handle, void* outTone);
    using nvspFrontend_setFrameExDefaults_t = void (__cdecl*)(void* handle, double creakiness, double breathiness, double jitter, double shimmer, double sharpness);
    using nvspFrontend_getABIVersion_t = int (__cdecl*)(void);

    // Text parser API (ABI v4+) — enables CMU Dict stress correction.
    using nvspFrontend_queueIPA_ExWithText_t = int (__cdecl*)(void* handle, const char* textUtf8, const char* ipaUtf8, double speed, double basePitch, double inflection, const char* clauseTypeUtf8, int userIndexBase, nvspFrontend_frame_ex_cb_t cb, void* userData);

    nvspFrontend_create_t nvspFrontend_create_ = nullptr;
    nvspFrontend_destroy_t nvspFrontend_destroy_ = nullptr;
    nvspFrontend_setLanguage_t nvspFrontend_setLanguage_ = nullptr;
    nvspFrontend_queueIPA_t nvspFrontend_queueIPA_ = nullptr;
    nvspFrontend_getLastError_t nvspFrontend_getLastError_ = nullptr;

    // Extended (may be null on older DLLs)
    nvspFrontend_queueIPA_Ex_t nvspFrontend_queueIPA_Ex_ = nullptr;
    nvspFrontend_setVoiceProfile_t nvspFrontend_setVoiceProfile_ = nullptr;
    nvspFrontend_getVoiceProfile_t nvspFrontend_getVoiceProfile_ = nullptr;
    nvspFrontend_getVoiceProfileNames_t nvspFrontend_getVoiceProfileNames_ = nullptr;
    nvspFrontend_getVoicingTone_t nvspFrontend_getVoicingTone_ = nullptr;
    nvspFrontend_setFrameExDefaults_t nvspFrontend_setFrameExDefaults_ = nullptr;
    nvspFrontend_getABIVersion_t nvspFrontend_getABIVersion_ = nullptr;
    nvspFrontend_queueIPA_ExWithText_t nvspFrontend_queueIPA_ExWithText_ = nullptr;

    // --- function pointers (libespeak-ng.dll) ---
    using espeak_Initialize_t = int (__cdecl*)(int output, int buflength, const char* path, int options);
    using espeak_SetVoiceByName_t = int (__cdecl*)(const char* name);
    using espeak_TextToPhonemes_t = const char* (__cdecl*)(const void** textptr, int textmode, int phonememode);
    using espeak_Terminate_t = int (__cdecl*)();
    using espeak_Info_t = const char* (__cdecl*)(const char** path_data);
    using espeak_ng_InitializePath_t = void (__cdecl*)(const char* path);

    espeak_Initialize_t espeak_Initialize_ = nullptr;
    espeak_SetVoiceByName_t espeak_SetVoiceByName_ = nullptr;
    espeak_TextToPhonemes_t espeak_TextToPhonemes_ = nullptr;
    espeak_Terminate_t espeak_Terminate_ = nullptr;
    espeak_Info_t espeak_Info_ = nullptr;
    espeak_ng_InitializePath_t espeak_ng_InitializePath_ = nullptr;

    // eSpeak is process-global; guard calls.
    static std::mutex& espeak_mutex();
    static bool is_espeak_initialized(HMODULE mod);
    static void mark_espeak_initialized(HMODULE mod);
    static void unmark_espeak_initialized(HMODULE mod);

    // Frame callback used by nvspFrontend_queueIPA_ (legacy).
    static void __cdecl frontend_frame_cb(void* userData, const void* frameOrNull, double durationMs, double fadeMs, int userIndex);

    // Extended frame callback used by nvspFrontend_queueIPA_Ex_ (ABI v2+).
    static void __cdecl frontend_frame_ex_cb(void* userData, const void* frameOrNull, const void* frameExOrNull, double durationMs, double fadeMs, int userIndex);

    // Current eSpeak voice name (per-runtime; setting is global but we track to avoid redundant calls).
    std::string current_espeak_voice_;

    // Reusable work buffer for IPA generation to reduce per-utterance allocations.
    std::string ipa_buf_;

    // Crash/reinit throttle. If eSpeak crashes repeatedly on a specific input,
    // repeatedly unloading/reloading the DLL becomes extremely expensive.
    // We back off for a short period after multiple crashes.
    std::chrono::steady_clock::time_point espeak_disable_until_{};
    std::chrono::steady_clock::time_point espeak_last_crash_{};
    int espeak_crash_streak_ = 0;

    bool espeak_needs_reinit_ = false;
};

} // namespace tgsb
} // namespace TGSpeech
