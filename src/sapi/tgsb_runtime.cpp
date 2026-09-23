/*
TGSpeechBox — Synthesis pipeline for SAPI (statically linked).
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "tgsb_runtime.hpp"
#include "voicingToneCompose.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <climits>
#include <cmath>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>

#include "debug_log.h"
#include "utils.hpp"
#include "tgsb_settings.hpp"

namespace TGSpeech {
namespace tgsb {

namespace {

constexpr int k_default_lcid = 0x0409; // en-US

// eSpeak constants (stable in speak_lib.h).
constexpr int k_espeak_chars_wchar = 3; // espeakCHARS_WCHAR
constexpr int k_espeak_phoneme_mode_ipa = 0x36100 + 0x82;

inline std::string wide_to_utf8(const std::wstring& w)
{
    return utils::wstring_to_string(w);
}

inline std::string wide_to_utf8(const std::filesystem::path& p)
{
    return utils::wstring_to_string(p.wstring());
}

std::wstring strip_filename(const std::wstring& path)
{
    const auto pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L".";
    }
    return path.substr(0, pos);
}

std::wstring join_path(const std::wstring& a, const std::wstring& b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

bool path_exists(const std::wstring& path)
{
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES;
}

bool dir_exists(const std::wstring& path)
{
    const DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

std::wstring parent_dir(std::wstring dir)
{
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) {
        dir.pop_back();
    }
    return strip_filename(dir);
}

std::wstring detect_base_dir(const std::wstring& module_dir)
{
    const std::wstring parent = parent_dir(module_dir);
    const std::wstring candidates[] = { module_dir, parent };
    for (const auto& c : candidates) {
        if (c.empty()) continue;
        if (dir_exists(join_path(c, L"packs")) || dir_exists(join_path(c, L"espeak-ng-data"))) {
            return c;
        }
    }
    return module_dir;
}

std::wstring detect_espeak_data_dir(const std::wstring& module_dir, const std::wstring& base_dir)
{
    const std::wstring parent = parent_dir(module_dir);
    const std::wstring candidates[] = {
        join_path(module_dir, L"espeak-ng-data"),
        join_path(base_dir, L"espeak-ng-data"),
        join_path(parent, L"espeak-ng-data"),
    };

    for (const auto& c : candidates) {
        if (c.empty()) continue;
        if (dir_exists(c)) return c;
    }
    return L"";
}

std::wstring to_windows_locale_name(std::wstring tag)
{
    std::replace(tag.begin(), tag.end(), L'_', L'-');
    if (_wcsicmp(tag.c_str(), L"default") == 0) {
        return L"en-US";
    }
    auto dash = tag.find(L'-');
    if (dash == std::wstring::npos) {
        std::transform(tag.begin(), tag.end(), tag.begin(), ::towlower);
        return tag;
    }
    std::wstring lang = tag.substr(0, dash);
    std::wstring rest = tag.substr(dash + 1);
    std::transform(lang.begin(), lang.end(), lang.begin(), ::towlower);
    std::transform(rest.begin(), rest.end(), rest.begin(), ::towupper);
    return lang + L"-" + rest;
}

std::wstring lcid_to_hex(LCID lcid)
{
    wchar_t buf[16] = {};
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%X", static_cast<unsigned int>(lcid));
    return std::wstring(buf);
}

struct frame_queue_ctx {
    runtime* rt = nullptr;
    const speak_params* params = nullptr;
};

// Built-in presets (always available, handled by apply_preset_and_volume).
const std::vector<std::wstring> k_builtin_presets = {
    L"Adam", L"Benjamin", L"Caleb", L"David", L"Robert"
};

const std::wstring k_voice_profile_prefix = L"profile:";

} // namespace

// ------------ Frame callbacks ------------

void __cdecl runtime::frontend_frame_cb(void* userData, const nvspFrontend_Frame* frameOrNull, double durationMs, double fadeMs, int userIndex)
{
    auto* ctx = static_cast<frame_queue_ctx*>(userData);
    if (!ctx || !ctx->rt || !ctx->params) return;

    runtime& rt = *ctx->rt;

    const auto ms_to_samples = [&](double ms) -> unsigned int {
        if (ms <= 0.0) return 0;
        return static_cast<unsigned int>(std::ceil(ms * static_cast<double>(rt.sample_rate()) / 1000.0));
    };

    unsigned int minSamples = ms_to_samples(durationMs);
    unsigned int fadeSamples = ms_to_samples(fadeMs);
    if (minSamples == 0) minSamples = 1;
    if (fadeSamples == 0) fadeSamples = 1;

    if (!rt.speech_player_) return;
    rt.queued_samples_ += minSamples;

    if (!frameOrNull) {
        speechPlayer_queueFrame(rt.speech_player_, nullptr, minSamples, fadeSamples, userIndex, false);
        return;
    }

    // nvspFrontend_Frame and speechPlayer_frame_t are layout-compatible (both double[47]).
    speechPlayer_frame_t frame;
    static_assert(sizeof(frame) == sizeof(*frameOrNull), "Frame layout mismatch");
    memcpy(&frame, frameOrNull, sizeof(frame));
    rt.apply_preset_and_volume(&frame, *ctx->params);
    speechPlayer_queueFrame(rt.speech_player_, &frame, minSamples, fadeSamples, userIndex, false);
}

void __cdecl runtime::frontend_frame_ex_cb(void* userData, const nvspFrontend_Frame* frameOrNull, const nvspFrontend_FrameEx* frameExOrNull, double durationMs, double fadeMs, int userIndex)
{
    auto* ctx = static_cast<frame_queue_ctx*>(userData);
    if (!ctx || !ctx->rt || !ctx->params) return;

    runtime& rt = *ctx->rt;

    const auto ms_to_samples = [&](double ms) -> unsigned int {
        if (ms <= 0.0) return 0;
        return static_cast<unsigned int>(std::ceil(ms * static_cast<double>(rt.sample_rate()) / 1000.0));
    };

    unsigned int minSamples = ms_to_samples(durationMs);
    unsigned int fadeSamples = ms_to_samples(fadeMs);
    if (minSamples == 0) minSamples = 1;
    if (fadeSamples == 0) fadeSamples = 1;

    if (!rt.speech_player_) return;
    rt.queued_samples_ += minSamples;

    if (!frameOrNull) {
        speechPlayer_queueFrameEx(rt.speech_player_, nullptr, nullptr, 0, minSamples, fadeSamples, userIndex, false);
        return;
    }

    // nvspFrontend_Frame and speechPlayer_frame_t are layout-compatible (both double[47]).
    speechPlayer_frame_t frame;
    static_assert(sizeof(frame) == sizeof(*frameOrNull), "Frame layout mismatch");
    memcpy(&frame, frameOrNull, sizeof(frame));
    rt.apply_preset_and_volume(&frame, *ctx->params);

    // nvspFrontend_FrameEx and speechPlayer_frameEx_t are layout-compatible (both 29 doubles).
    const unsigned int frameExSize = frameExOrNull ? static_cast<unsigned int>(sizeof(speechPlayer_frameEx_t)) : 0;
    speechPlayer_queueFrameEx(rt.speech_player_, &frame, reinterpret_cast<const speechPlayer_frameEx_t*>(frameExOrNull), frameExSize, minSamples, fadeSamples, userIndex, false);
}

// ------------ Installation/path helpers ------------

std::wstring get_this_module_dir()
{
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&get_this_module_dir),
                            &mod) || !mod) {
        return L".";
    }

    wchar_t path[MAX_PATH + 1] = {};
    const DWORD n = GetModuleFileNameW(mod, path, MAX_PATH);
    if (n == 0) return L".";
    path[n] = L'\0';
    return strip_filename(path);
}

std::vector<std::wstring> get_installed_language_tags()
{
    const std::wstring module_dir = get_this_module_dir();
    const std::wstring base = detect_base_dir(module_dir);
    const auto& settings = get_settings_cached(base);

    const std::wstring glob = join_path(join_path(join_path(base, L"packs"), L"lang"), L"*.yaml");

    std::vector<std::wstring> tags;

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(glob.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = fd.cFileName;
            if (name.size() >= 5 && _wcsicmp(name.c_str() + (name.size() - 5), L".yaml") == 0) {
                name.resize(name.size() - 5);
            }
            if (!name.empty()) {
                tags.push_back(name);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    if (tags.empty()) {
        tags = { L"en-us", L"en", L"es", L"fr", L"de", L"it", L"ru", L"pl", L"pt-br", L"hu" };
    }

    if (!settings.excluded_lang_tags.empty()) {
        tags.erase(std::remove_if(tags.begin(), tags.end(), [&](const std::wstring& t) {
            return settings.excluded_lang_tags.find(normalize_lang_tag(t)) != settings.excluded_lang_tags.end();
        }), tags.end());
    }

    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());

    // Hide base language tags when regional variants exist.
    {
        std::vector<std::wstring> filtered;
        filtered.reserve(tags.size());
        for (const auto& tag : tags) {
            if (_wcsicmp(tag.c_str(), L"default") == 0) continue;
            if (tag.find(L'-') == std::wstring::npos && tag.find(L'_') == std::wstring::npos) {
                const std::wstring prefix = tag + L"-";
                bool has_regional = false;
                for (const auto& other : tags) {
                    if (other.size() > prefix.size() && _wcsnicmp(other.c_str(), prefix.c_str(), prefix.size()) == 0) {
                        has_regional = true;
                        break;
                    }
                }
                if (has_regional) continue;
            }
            filtered.push_back(tag);
        }
        tags = std::move(filtered);
    }

    return tags;
}

std::wstring get_language_display_name(const std::wstring& lang_tag)
{
    const std::wstring locale = to_windows_locale_name(lang_tag);
    wchar_t buf[256] = {};
    const int n = GetLocaleInfoEx(locale.c_str(), LOCALE_SLOCALIZEDDISPLAYNAME, buf, static_cast<int>(_countof(buf)));
    if (n > 0) return std::wstring(buf);
    return lang_tag;
}

std::wstring lang_tag_to_lcid_hex(const std::wstring& lang_tag)
{
    const std::wstring locale = to_windows_locale_name(lang_tag);
    const LCID lcid = LocaleNameToLCID(locale.c_str(), 0);
    if (lcid == 0) return lcid_to_hex(k_default_lcid);
    return lcid_to_hex(lcid);
}

std::vector<std::wstring> get_voice_profile_names()
{
    std::vector<std::wstring> result;
    for (const auto& preset : k_builtin_presets) {
        result.push_back(preset);
    }

    try {
        const std::wstring module_dir = get_this_module_dir();
        const std::wstring base = detect_base_dir(module_dir);
        const std::string pack_dir_utf8 = utils::wstring_to_string(base);

        // Direct call — frontend is statically linked.
        void* frontend = nvspFrontend_create(pack_dir_utf8.c_str());
        if (!frontend) return result;

        // Must call setLanguage to load the pack before querying profiles.
        int langOk = nvspFrontend_setLanguage(frontend, "default");
        if (!langOk) {
            nvspFrontend_destroy(frontend);
            return result;
        }

        const char* names_utf8 = nvspFrontend_getVoiceProfileNames(frontend);
        if (names_utf8 && names_utf8[0] != '\0') {
            std::string names(names_utf8);
            size_t start = 0;
            while (start < names.size()) {
                size_t end = names.find('\n', start);
                if (end == std::string::npos) end = names.size();
                if (end > start) {
                    std::string name = names.substr(start, end - start);
                    if (!name.empty()) {
                        result.push_back(k_voice_profile_prefix + utils::string_to_wstring(name));
                    }
                }
                start = end + 1;
            }
        }

        nvspFrontend_destroy(frontend);
    }
    catch (...) {
        // On any exception, return built-ins only.
    }

    return result;
}

// ------------ Runtime lifecycle ------------

runtime::runtime()
    : module_dir_(get_this_module_dir())
{
    base_dir_ = detect_base_dir(module_dir_);
    espeak_data_dir_ = detect_espeak_data_dir(module_dir_, base_dir_);
    (void)get_settings_cached(base_dir_);

    DEBUG_LOG("runtime: module_dir='%ls' base_dir='%ls' espeak_data_dir='%ls'",
        module_dir_.c_str(), base_dir_.c_str(), espeak_data_dir_.c_str());
}

runtime::~runtime()
{
    if (frontend_) {
        nvspFrontend_destroy(frontend_);
        frontend_ = nullptr;
    }

    if (speech_player_) {
        speechPlayer_terminate(speech_player_);
        speech_player_ = nullptr;
    }

    if (espeak_initialized_) {
        espeak_Terminate();
        espeak_initialized_ = false;
    }
}

HRESULT runtime::ensure_initialized()
{
    if (speech_player_ && frontend_ && espeak_initialized_)
        return S_OK;

    // 0. Read sample rate from settings (before creating speechPlayer).
    {
        const auto& s = get_settings_cached(base_dir_);
        if (s.sample_rate == 11025 || s.sample_rate == 16000 ||
            s.sample_rate == 22050 || s.sample_rate == 44100) {
            sample_rate_ = s.sample_rate;
        }
    }

    // 1. Init speechPlayer (direct call).
    if (!speech_player_) {
        speech_player_ = speechPlayer_initialize(sample_rate_);
        if (!speech_player_) return E_FAIL;
    }

    // 2. Init frontend (direct call).
    if (!frontend_) {
        auto packs_dir = join_path(base_dir_, L"packs");
        auto packs_utf8 = wide_to_utf8(packs_dir);
        // nvspFrontend_create expects the base dir (parent of packs/), not packs/ itself.
        auto base_utf8 = wide_to_utf8(base_dir_);
        frontend_ = nvspFrontend_create(base_utf8.c_str());
        if (!frontend_) return E_FAIL;
    }

    // 3. Init eSpeak (direct call).
    if (!espeak_initialized_) {
        HRESULT hr = init_espeak();
        if (FAILED(hr)) return hr;
    }

    return S_OK;
}

HRESULT runtime::init_espeak()
{
    if (!espeak_data_dir_.empty()) {
        auto data_utf8 = wide_to_utf8(espeak_data_dir_);
        // Strip the trailing "espeak-ng-data" from the path — espeak_Initialize
        // expects the PARENT directory and appends "espeak-ng-data" itself.
        auto parent_utf8 = wide_to_utf8(parent_dir(espeak_data_dir_));
        espeak_ng_InitializePath(parent_utf8.c_str());
    }

    auto data_utf8 = espeak_data_dir_.empty() ? std::string() : wide_to_utf8(parent_dir(espeak_data_dir_));
    int sr = espeak_Initialize(
        AUDIO_OUTPUT_RETRIEVAL,
        0,
        data_utf8.empty() ? nullptr : data_utf8.c_str(),
        espeakINITIALIZE_DONT_EXIT);

    if (sr <= 0) {
        DEBUG_LOG("init_espeak: espeak_Initialize failed (rc=%d)", sr);
        return E_FAIL;
    }

    espeak_initialized_ = true;
    DEBUG_LOG("init_espeak: OK (sr=%d)", sr);
    return S_OK;
}

// ------------ Language and voice profile management ------------

HRESULT runtime::set_language(const std::wstring& lang_tag)
{
    HRESULT hr = ensure_initialized();
    if (FAILED(hr)) return hr;

    const std::wstring tag = lang_tag.empty() ? L"en-us" : lang_tag;
    const std::string tag_utf8 = utils::wstring_to_string(tag);

    std::wstring espeak_tag = tag;
    if (_wcsicmp(espeak_tag.c_str(), L"default") == 0) {
        espeak_tag = L"en-us";
    }
    if (_wcsicmp(espeak_tag.c_str(), L"en-ca") == 0) {
        espeak_tag = L"en-us";
    }
    const std::string espeak_tag_utf8 = utils::wstring_to_string(espeak_tag);

    // Frontend language.
    if (frontend_ && tag != current_lang_tag_) {
        if (!nvspFrontend_setLanguage(frontend_, tag_utf8.c_str())) {
            const char* err = nvspFrontend_getLastError(frontend_);
            DEBUG_LOG("set_language: frontend failed for '%s': %s", tag_utf8.c_str(), err ? err : "(null)");
            return E_FAIL;
        }
    }

    // eSpeak voice selection.
    auto select_espeak_voice = [&](const std::string& desired) -> bool {
        std::lock_guard<std::mutex> lock(espeak_guard_);

        if (!current_espeak_voice_.empty() && _stricmp(current_espeak_voice_.c_str(), desired.c_str()) == 0) {
            return true;
        }

        // Strategy 1: enumerate voices, find full identifier (NVDA approach).
        auto try_set_by_list = [&](const std::string& lang) -> bool {
            if (lang.empty()) return false;

            const espeak_VOICE** voices = espeak_ListVoices(nullptr);
            if (!voices) return false;

            const char* exact_id = nullptr;
            int exact_priority = 99;
            const char* base_id = nullptr;
            int base_priority = 99;

            std::string base_lang = lang;
            {
                const size_t dash = base_lang.find_first_of("-_");
                if (dash != std::string::npos) base_lang.resize(dash);
            }

            for (int ix = 0; voices[ix] != nullptr; ++ix) {
                const char* langs = voices[ix]->languages;
                if (!langs) continue;
                const char* p = langs;
                while (*p) {
                    const int priority = static_cast<unsigned char>(*p);
                    const char* ltag = p + 1;
                    if (_stricmp(ltag, lang.c_str()) == 0) {
                        if (priority < exact_priority) {
                            exact_priority = priority;
                            exact_id = voices[ix]->identifier;
                        }
                    } else if (_stricmp(ltag, base_lang.c_str()) == 0) {
                        if (priority < base_priority) {
                            base_priority = priority;
                            base_id = voices[ix]->identifier;
                        }
                    }
                    p = ltag + strlen(ltag) + 1;
                }
            }

            const char* chosen_id = exact_id ? exact_id : base_id;
            if (!chosen_id) return false;

            const int rc = espeak_SetVoiceByName(chosen_id);
            if (rc == 0) {
                current_espeak_voice_ = lang;
                resolved_espeak_identifier_ = chosen_id;
                return true;
            }
            return false;
        };

        // Strategy 2: SetVoiceByName with short tag.
        auto try_set_by_name = [&](const std::string& name) -> bool {
            if (name.empty()) return false;
            const int rc = espeak_SetVoiceByName(name.c_str());
            if (rc == 0) {
                current_espeak_voice_ = name;
                resolved_espeak_identifier_ = name;
                return true;
            }
            return false;
        };

        // Strategy 3: SetVoiceByProperties.
        auto try_set_by_language = [&](const std::string& lang) -> bool {
            if (lang.empty()) return false;
            espeak_VOICE voice_spec{};
            voice_spec.languages = lang.c_str();
            const int rc = espeak_SetVoiceByProperties(&voice_spec);
            if (rc == 0) {
                current_espeak_voice_ = lang;
                resolved_espeak_identifier_.clear();
                return true;
            }
            return false;
        };

        if (try_set_by_list(desired)) return true;
        if (try_set_by_name(desired)) return true;
        if (try_set_by_language(desired)) return true;

        // Try base language.
        std::string base = desired;
        const size_t pos = base.find_first_of("-_");
        if (pos != std::string::npos) {
            base.resize(pos);
            if (try_set_by_list(base)) return true;
            if (try_set_by_name(base)) return true;
            if (try_set_by_language(base)) return true;
        }

        // Last resort: English.
        if (try_set_by_list("en")) return true;
        return try_set_by_name("en") || try_set_by_language("en");
    };

    if (!select_espeak_voice(espeak_tag_utf8)) {
        DEBUG_LOG("set_language: eSpeak voice selection failed for '%s'", espeak_tag_utf8.c_str());
    }

    current_lang_tag_ = tag;
    return S_OK;
}

HRESULT runtime::set_voice_profile(const std::wstring& profile_name)
{
    HRESULT hr = ensure_initialized();
    if (FAILED(hr)) return hr;

    if (current_profile_name_ == profile_name) return S_OK;

    if (frontend_) {
        const std::string name_utf8 = profile_name.empty() ? "" : utils::wstring_to_string(profile_name);
        if (!nvspFrontend_setVoiceProfile(frontend_, name_utf8.c_str())) {
            DEBUG_LOG("set_voice_profile: failed for '%ls'", profile_name.c_str());
        }
    }

    current_profile_name_ = profile_name;
    apply_voicing_tone_if_available();
    return S_OK;
}

void runtime::apply_voicing_tone_if_available()
{
    has_voicing_tone_ = false;
    memset(&cached_voicing_tone_, 0, sizeof(cached_voicing_tone_));

    if (!speech_player_) return;

    speechPlayer_voicingTone_t dsp_tone = speechPlayer_getDefaultVoicingTone();

    // Get base tone from voice profile (if any).
    if (frontend_) {
        nvspFrontend_VoicingTone tone{};
        const int has_tone = nvspFrontend_getVoicingTone(frontend_, &tone);
        if (has_tone) {
            cached_voicing_tone_ = tone;
            has_voicing_tone_ = true;
            dsp_tone.voicingPeakPos          = tone.voicingPeakPos;
            dsp_tone.voicedPreEmphA           = tone.voicedPreEmphA;
            dsp_tone.voicedPreEmphMix         = tone.voicedPreEmphMix;
            dsp_tone.highShelfGainDb          = tone.highShelfGainDb;
            dsp_tone.highShelfFcHz            = tone.highShelfFcHz;
            dsp_tone.highShelfQ               = tone.highShelfQ;
            dsp_tone.voicedTiltDbPerOct       = tone.voicedTiltDbPerOct;
            dsp_tone.noiseGlottalModDepth     = tone.noiseGlottalModDepth;
            dsp_tone.pitchSyncF1DeltaHz       = tone.pitchSyncF1DeltaHz;
            dsp_tone.pitchSyncB1DeltaHz       = tone.pitchSyncB1DeltaHz;
            dsp_tone.speedQuotient            = tone.speedQuotient;
            dsp_tone.aspirationTiltDbPerOct   = tone.aspirationTiltDbPerOct;
            dsp_tone.cascadeBwScale           = tone.cascadeBwScale;
            dsp_tone.tremorDepth              = tone.tremorDepth;
            dsp_tone.nasalBwScale             = tone.nasalBwScale;
            dsp_tone.f4FreqScale              = tone.f4FreqScale;
            dsp_tone.nasalGainScale           = tone.nasalGainScale;
        }
    }

    // Apply slider offsets from settings (same math as NVDA voicing_tone.py).
    const auto& s = get_settings_cached(base_dir_);
    auto clamp = [](double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); };

    if (has_voicing_tone_) {
        // A voice profile's voicingTone is the base: a setting the listener
        // has not set (-1) leaves the stored value alone, a set one composes
        // with it (src/voicingToneCompose.h; the NVDA driver uses the same
        // rule), so a profile sounds as saved at default settings.
        auto sqValue = [](double v) { return v <= 50.0 ? 0.5 + (v / 50.0) * 1.5 : 2.0 + ((v - 50.0) / 50.0) * 2.0; };
        auto bwValue = [](double v) { return v <= 50.0 ? 2.0 - (v / 50.0) * 1.0 : 1.0 - ((v - 50.0) / 50.0) * 0.7; };
        auto hsValue = [](double v) { return v <= 50.0 ? 1.25 - (v / 50.0) * 0.25 : 1.0 - ((v - 50.0) / 50.0) * 0.15; };
        speechPlayer_composeListenerSettings(&dsp_tone,
            s.voiceTilt >= 0 ? (s.voiceTilt - 50.0) * (24.0 / 50.0) : 0.0,
            s.noiseGlottalMod >= 0 ? s.noiseGlottalMod / 100.0 : 0.0,
            s.pitchSyncF1 >= 0 ? (s.pitchSyncF1 - 50.0) * 1.2 : 0.0,
            s.pitchSyncB1 >= 0 ? (s.pitchSyncB1 - 50.0) * 1.0 : 0.0,
            s.speedQuotient >= 0 ? sqValue(static_cast<double>(s.speedQuotient)) : 2.0,
            s.aspirationTilt >= 0 ? (s.aspirationTilt - 50.0) * 0.24 : 0.0,
            s.cascadeBwScale >= 0 ? clamp(bwValue(static_cast<double>(s.cascadeBwScale)), 0.3, 2.0) : 1.0,
            s.voiceTremor >= 0 ? clamp((s.voiceTremor / 100.0) * 0.4, 0.0, 0.5) : 0.0,
            1.0,
            s.headSize >= 0 ? clamp(hsValue(static_cast<double>(s.headSize)), 0.7, 1.5) : 1.0,
            1.0);
    } else {
        if (s.voiceTilt >= 0) {
            double offset = (s.voiceTilt - 50.0) * (24.0 / 50.0);
            dsp_tone.voicedTiltDbPerOct = clamp(dsp_tone.voicedTiltDbPerOct + offset, -24.0, 24.0);
        }
        if (s.noiseGlottalMod >= 0) {
            dsp_tone.noiseGlottalModDepth = s.noiseGlottalMod / 100.0;
        }
        if (s.pitchSyncF1 >= 0) {
            dsp_tone.pitchSyncF1DeltaHz = (s.pitchSyncF1 - 50.0) * 1.2;
        }
        if (s.pitchSyncB1 >= 0) {
            dsp_tone.pitchSyncB1DeltaHz = (s.pitchSyncB1 - 50.0) * 1.0;
        }
        if (s.speedQuotient >= 0) {
            double sq = static_cast<double>(s.speedQuotient);
            if (sq <= 50.0)
                dsp_tone.speedQuotient = 0.5 + (sq / 50.0) * 1.5;
            else
                dsp_tone.speedQuotient = 2.0 + ((sq - 50.0) / 50.0) * 2.0;
        }
        if (s.aspirationTilt >= 0) {
            dsp_tone.aspirationTiltDbPerOct = (s.aspirationTilt - 50.0) * 0.24;
        }
        if (s.cascadeBwScale >= 0) {
            double bw = static_cast<double>(s.cascadeBwScale);
            if (bw <= 50.0)
                dsp_tone.cascadeBwScale = 2.0 - (bw / 50.0) * 1.0;
            else
                dsp_tone.cascadeBwScale = 1.0 - ((bw - 50.0) / 50.0) * 0.7;
            dsp_tone.cascadeBwScale = clamp(dsp_tone.cascadeBwScale, 0.3, 2.0);
        }
        if (s.voiceTremor >= 0) {
            dsp_tone.tremorDepth = clamp((s.voiceTremor / 100.0) * 0.4, 0.0, 0.5);
        }
        if (s.headSize >= 0) {
            double hs = static_cast<double>(s.headSize);
            if (hs <= 50.0)
                dsp_tone.f4FreqScale = 1.25 - (hs / 50.0) * 0.25;
            else
                dsp_tone.f4FreqScale = 1.0 - ((hs - 50.0) / 50.0) * 0.15;
            dsp_tone.f4FreqScale = clamp(dsp_tone.f4FreqScale, 0.7, 1.5);
        }
    }
    if (s.chorusDepth >= 0) {
        dsp_tone.chorusDepth = clamp(s.chorusDepth / 100.0, 0.0, 1.0);
    }
    if (s.chorusDetune >= 0) {
        dsp_tone.chorusDetuneHz = 0.5 + (s.chorusDetune / 100.0) * 4.5;
        dsp_tone.chorusDetuneHz = clamp(dsp_tone.chorusDetuneHz, 0.5, 5.0);
    }

    speechPlayer_setVoicingTone(speech_player_, &dsp_tone);

    // Apply pitch mode override from settings.
    if (frontend_ && !s.pitchMode.empty()) {
        std::string yaml = "legacyPitchMode: " + utils::wstring_to_string(s.pitchMode) + "\n";
        if (s.pitchInflectionScale >= 0) {
            double scale = s.pitchInflectionScale / 100.0 * 2.0; // 0-100 → 0.0-2.0
            char buf[64];
            std::snprintf(buf, sizeof(buf), "legacyPitchInflectionScale: %.4f\n", scale);
            yaml += buf;
        }
        nvspFrontend_applySettingOverrides(frontend_, yaml.c_str());
    } else if (frontend_ && s.pitchInflectionScale >= 0) {
        double scale = s.pitchInflectionScale / 100.0 * 2.0;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "legacyPitchInflectionScale: %.4f\n", scale);
        nvspFrontend_applySettingOverrides(frontend_, buf);
    }

    // Apply FrameEx defaults from settings.
    if (frontend_) {
        double creakiness  = (s.frameExCreakiness >= 0)  ? s.frameExCreakiness / 100.0  : 0.0;
        double breathiness = (s.frameExBreathiness >= 0) ? s.frameExBreathiness / 100.0 : 0.0;
        double jitter      = (s.frameExJitter >= 0)      ? s.frameExJitter / 100.0      : 0.0;
        double shimmer     = (s.frameExShimmer >= 0)     ? s.frameExShimmer / 100.0     : 0.0;
        double sharpness   = 1.0;
        if (s.frameExSharpness >= 0) {
            sharpness = std::pow(2.0, (s.frameExSharpness - 50.0) / 25.0);
        }
        nvspFrontend_setFrameExDefaults(frontend_, creakiness, breathiness, jitter, shimmer, sharpness);
    }
}

// ------------ Text to IPA ------------

void runtime::text_to_ipa_utf8(const std::wstring& text, std::string& out_ipa)
{
    out_ipa.clear();
    if (text.empty()) return;

    if (current_lang_tag_.empty()) {
        (void)set_language(L"en-us");
    }

    const size_t want = text.size() * 4;
    if (out_ipa.capacity() < want) {
        out_ipa.reserve(want);
    }

    // Lock around SetVoiceByName + TextToPhonemes for atomicity.
    std::lock_guard<std::mutex> lock(espeak_guard_);

    // Re-apply voice under the same lock as TextToPhonemes.
    if (!resolved_espeak_identifier_.empty()) {
        espeak_SetVoiceByName(resolved_espeak_identifier_.c_str());
    }

    const void* text_ptr = static_cast<const void*>(text.c_str());
    while (text_ptr) {
        const auto* w = reinterpret_cast<const wchar_t*>(text_ptr);
        if (!w || *w == L'\0') break;

        const char* phon = espeak_TextToPhonemes(
            &text_ptr,
            k_espeak_chars_wchar,
            k_espeak_phoneme_mode_ipa);

        if (!phon) break;
        out_ipa.append(phon);
    }

    // Trim whitespace.
    auto is_ws = [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; };
    size_t start = 0;
    while (start < out_ipa.size() && is_ws(out_ipa[start])) ++start;
    if (start > 0) out_ipa.erase(0, start);
    while (!out_ipa.empty() && is_ws(out_ipa.back())) out_ipa.pop_back();

    DEBUG_LOG("text_to_ipa: voice='%s' ipa='%.200s'",
              current_espeak_voice_.c_str(), out_ipa.c_str());
}

// ------------ Synthesis pipeline ------------

HRESULT runtime::queue_text(const std::wstring& text, const speak_params& params)
{
    queued_samples_ = 0;
    if (text.empty()) return S_OK;

    HRESULT hr = ensure_initialized();
    if (FAILED(hr)) return hr;

    // Re-apply voicing tone + FrameEx from settings on every utterance.
    // get_settings_cached() inside checks mtime — cheap when file unchanged.
    apply_voicing_tone_if_available();

    // Determine if this is a built-in preset or a voice profile.
    const std::wstring profilePrefix = L"profile:";
    const bool isProfile = params.preset_name.rfind(profilePrefix, 0) == 0;

    if (isProfile) {
        std::wstring profileName = params.preset_name.substr(profilePrefix.length());
        if (profileName != current_profile_name_) {
            (void)set_voice_profile(profileName);
        }
    } else {
        if (!current_profile_name_.empty()) {
            if (frontend_) {
                nvspFrontend_setVoiceProfile(frontend_, "");
            }
            current_profile_name_.clear();
            has_voicing_tone_ = false;
        }
    }

    // Pre-eSpeak text normalization: compound splitting, date ordinals, etc.
    std::wstring prepared_text = text;
    if (frontend_) {
        const std::string text_utf8 = utils::wstring_to_string(text);
        char* prepared = nvspFrontend_prepareText(frontend_, text_utf8.c_str());
        if (prepared) {
            prepared_text = utils::string_to_wstring(std::string(prepared));
            nvspFrontend_freeString(prepared);
        }
    }

    ipa_buf_.clear();
    text_to_ipa_utf8(prepared_text, ipa_buf_);
    if (ipa_buf_.empty()) return S_OK;

    if (!frontend_) return E_FAIL;

    char clause[2] = { params.clause_type ? params.clause_type : '.', 0 };

    frame_queue_ctx ctx;
    ctx.rt = this;
    ctx.params = &params;

    // Always use the latest API (ExWithText) — frontend is statically linked.
    const std::string text_utf8 = utils::wstring_to_string(prepared_text);
    int ok = nvspFrontend_queueIPA_ExWithText(
        frontend_,
        text_utf8.c_str(),
        ipa_buf_.c_str(),
        params.speed,
        params.base_pitch,
        params.inflection,
        clause,
        params.user_index_base,
        &runtime::frontend_frame_ex_cb,
        &ctx
    );

    if (!ok) {
        const char* err = nvspFrontend_getLastError(frontend_);
        DEBUG_LOG("queue_text: nvspFrontend_queueIPA_ExWithText failed: %s", err ? err : "(null)");
        return E_FAIL;
    }

    return S_OK;
}

int runtime::synthesize(int max_samples, sample* out_samples)
{
    if (!speech_player_ || max_samples <= 0 || !out_samples) return 0;
    return speechPlayer_synthesize(speech_player_, static_cast<unsigned int>(max_samples), out_samples);
}

void runtime::set_time_stretch(double factor)
{
    if (speech_player_)
        speechPlayer_setTimeStretch(
            static_cast<speechPlayer_handle_t>(speech_player_), factor);
}

void runtime::purge()
{
    if (!speech_player_) return;

    const auto ms_to_samples = [&](double ms) -> unsigned int {
        if (ms <= 0.0) return 0;
        return static_cast<unsigned int>(std::ceil(ms * static_cast<double>(sample_rate_) / 1000.0));
    };

    speechPlayer_queueFrame(speech_player_, nullptr, ms_to_samples(20.0), ms_to_samples(5.0), 0, true);
}

// ------------ Preset and volume ------------

void runtime::apply_preset_and_volume(void* frame_ptr, const speak_params& params)
{
    if (!frame_ptr) return;

    auto& f = *reinterpret_cast<speechPlayer_frame_t*>(frame_ptr);

    auto preset = params.preset_name;
    auto eq = [](const std::wstring& a, const wchar_t* b) {
        return _wcsicmp(a.c_str(), b) == 0;
    };

    if (eq(preset, L"Adam") || preset.empty()) {
        f.cb1 *= 1.3;
        f.pa6 *= 1.3;
        f.fricationAmplitude *= 0.85;
    }
    else if (eq(preset, L"Benjamin")) {
        f.cf1 *= 1.01;
        f.cf2 *= 1.02;
        f.cf4 = 3770;
        f.cf5 = 4100;
        f.cf6 = 5000;
        f.cfNP *= 0.9;
        f.cb1 *= 1.3;
        f.fricationAmplitude *= 0.7;
        f.pa6 *= 1.3;
    }
    else if (eq(preset, L"Caleb")) {
        f.aspirationAmplitude = 1;
        f.voiceAmplitude = 0;
    }
    else if (eq(preset, L"David")) {
        f.voicePitch *= 0.75;
        f.endVoicePitch *= 0.75;
        f.cf1 *= 0.90;
        f.cf2 *= 0.93;
        f.cf3 *= 0.95;
    }
    else if (eq(preset, L"Robert")) {
        f.voicePitch *= 1.10;
        f.endVoicePitch *= 1.10;
        f.cf1 *= 1.02;
        f.cf2 *= 1.06;
        f.cf3 *= 1.08;
        f.cf4 *= 1.08;
        f.cf5 *= 1.10;
        f.cf6 *= 1.05;
        f.cb1 *= 0.65;
        f.cb2 *= 0.68;
        f.cb3 *= 0.72;
        f.cb4 *= 0.75;
        f.cb5 *= 0.78;
        f.cb6 *= 0.80;
        f.glottalOpenQuotient = 0.30;
        f.voiceTurbulenceAmplitude *= 0.20;
        f.fricationAmplitude *= 0.75;
        f.parallelBypass *= 0.70;
        f.pa3 *= 1.08;
        f.pa4 *= 1.15;
        f.pa5 *= 1.20;
        f.pa6 *= 1.25;
        f.pb1 *= 0.72;
        f.pb2 *= 0.75;
        f.pb3 *= 0.78;
        f.pb4 *= 0.80;
        f.pb5 *= 0.82;
        f.pb6 *= 0.85;
        f.pf3 *= 1.06;
        f.pf4 *= 1.08;
        f.pf5 *= 1.10;
        f.pf6 *= 1.00;
        f.vibratoPitchOffset = 0.0;
        f.vibratoSpeed = 0.0;
    }

    // Volume scaling.
    double v = params.volume;
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;

    f.preFormantGain *= v;

    constexpr double k_output_gain_boost_at_max = 0.50;
    const double out_v = v * (1.0 + k_output_gain_boost_at_max * v);
    f.outputGain *= out_v;
}

} // namespace tgsb
} // namespace TGSpeech
