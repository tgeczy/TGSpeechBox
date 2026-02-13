/*
TGSpeechBox — DLL runtime loader and synthesis pipeline for SAPI.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "tgsb_runtime.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <climits>
#include <cmath>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <stdexcept>

#include "debug_log.h"
#include "utils.hpp"
#include "tgsb_settings.hpp"

namespace TGSpeech {
namespace tgsb {

namespace {

constexpr int k_default_lcid = 0x0409; // en-US

// eSpeak constants.
// These values are stable in speak_lib.h for eSpeak / eSpeak-NG.
// We deliberately embed the numeric values so we don't need the eSpeak headers at build time.
constexpr int k_espeak_chars_utf8 = 1;  // espeakCHARS_UTF8
constexpr int k_espeak_chars_wchar = 3; // espeakCHARS_WCHAR
constexpr int k_espeak_initialize_dont_exit = 0x8000; // espeakINITIALIZE_DONT_EXIT

// NVDA uses this exact mode for IPA conversion.
// In TGSpeechBox's NVDA driver (Python), this is 0x36100 + 0x82.
constexpr int k_espeak_phoneme_mode_ipa = 0x36100 + 0x82;

// We don't want eSpeak to open an audio device. "AUDIO_OUTPUT_RETRIEVAL" is typically 1.
constexpr int k_espeak_audio_output_retrieval = 1;

// Convert wide strings / filesystem paths to UTF-8.
// We keep this local so the rest of the code doesn't have to care whether the
// caller is passing std::wstring or std::filesystem::path.
inline std::string wide_to_utf8(const std::wstring& w)
{
    return utils::wstring_to_string(w);
}

inline std::string wide_to_utf8(const std::filesystem::path& p)
{
    return utils::wstring_to_string(p.wstring());
}

// --- eSpeak crash guards ---
//
// eSpeak-NG is native code and (in the real world) can AV if its data path isn't
// initialized correctly or the caller passes unexpected input. NVDA loads SAPI
// engines in-proc, so an AV here kills the whole host process.
//
// MSVC doesn't allow __try/__except in functions that require C++ object
// unwinding. Keep these helpers POD-only so the wrapper can compile cleanly.
constexpr int k_espeak_crash_rc = INT_MIN;

template <typename Fn>
static int safe_espeak_SetVoiceByName(
    Fn fn,
    const char* name,
    bool* out_crashed) noexcept
{
    if (out_crashed) *out_crashed = false;
    if (!fn) return -1;
#ifdef _MSC_VER
    __try {
        return fn(name);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (out_crashed) *out_crashed = true;
        return k_espeak_crash_rc;
    }
#else
    (void)out_crashed;
    return fn(name);
#endif
}

template <typename Fn>
static const char* safe_espeak_TextToPhonemes(
    Fn fn,
    const void** textptr,
    int textmode,
    int phonememode,
    bool* out_crashed) noexcept
{
    if (out_crashed) *out_crashed = false;
    if (!fn) return nullptr;
#ifdef _MSC_VER
    __try {
        return fn(textptr, textmode, phonememode);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (out_crashed) *out_crashed = true;
        return nullptr;
    }
#else
    (void)out_crashed;
    return fn(textptr, textmode, phonememode);
#endif
}

template <typename Fn>
static int safe_espeak_Terminate(
    Fn fn,
    bool* out_crashed) noexcept
{
    if (out_crashed) *out_crashed = false;
    if (!fn) return 0;
#ifdef _MSC_VER
    __try {
        return fn();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (out_crashed) *out_crashed = true;
        return k_espeak_crash_rc;
    }
#else
    (void)out_crashed;
    return fn();
#endif
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
    // Prefer the directory that contains shared resources. This lets us support
    // both:
    //  1) single-folder installs (dlls + packs + espeak data all in one dir)
    //  2) split installs:
    //      <root>\\x86\\TGSpeechSapi.dll
    //      <root>\\x64\\TGSpeechSapi.dll
    //      <root>\\packs\\...
    //      <root>\\espeak-ng-data\\...

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
    // Normalize separators.
    std::replace(tag.begin(), tag.end(), L'_', L'-');

    // Special case.
    if (_wcsicmp(tag.c_str(), L"default") == 0) {
        return L"en-US";
    }

    // Best-effort casing: language lower, region upper.
    // Examples: "en-us" -> "en-US", "pt-br" -> "pt-BR".
    auto dash = tag.find(L'-');
    if (dash == std::wstring::npos) {
        // "en" is acceptable to Windows APIs.
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
    // SAPI typically stores this as hex without a leading 0x.
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%X", static_cast<unsigned int>(lcid));
    return std::wstring(buf);
}

struct frame_queue_ctx {
    runtime* rt = nullptr;
    const speak_params* params = nullptr;
};

} // namespace

void __cdecl runtime::frontend_frame_cb(void* userData, const void* frameOrNull, double durationMs, double fadeMs, int userIndex)
{
    auto* ctx = static_cast<frame_queue_ctx*>(userData);
    if (!ctx || !ctx->rt || !ctx->params) {
        return;
    }

    runtime& rt = *ctx->rt;

    const auto ms_to_samples = [&](double ms) -> unsigned int {
        if (ms <= 0.0) return 0;
        const double s = ms * static_cast<double>(rt.sample_rate()) / 1000.0;
        return static_cast<unsigned int>(std::ceil(s));
    };

    // Some hosts (or malformed pack data) can lead to extremely small/zero
    // durations. SpeechPlayer can behave badly if we feed it a real (non-null)
    // frame with a zero min duration, so clamp to at least 1 sample.
    unsigned int minSamples = ms_to_samples(durationMs);
    unsigned int fadeSamples = ms_to_samples(fadeMs);
    if (minSamples == 0) minSamples = 1;
    if (fadeSamples == 0) fadeSamples = 1;

    if (!rt.speechPlayer_queueFrame_ || !rt.speech_player_) {
        return;
    }

    if (!frameOrNull) {
        rt.speechPlayer_queueFrame_(rt.speech_player_, nullptr, minSamples, fadeSamples, userIndex, false);
        return;
    }

    runtime::nvsp_frame frame = *reinterpret_cast<const runtime::nvsp_frame*>(frameOrNull);
    rt.apply_preset_and_volume(&frame, *ctx->params);
    rt.speechPlayer_queueFrame_(rt.speech_player_, &frame, minSamples, fadeSamples, userIndex, false);
}

void __cdecl runtime::frontend_frame_ex_cb(void* userData, const void* frameOrNull, const void* frameExOrNull, double durationMs, double fadeMs, int userIndex)
{
    auto* ctx = static_cast<frame_queue_ctx*>(userData);
    if (!ctx || !ctx->rt || !ctx->params) {
        return;
    }

    runtime& rt = *ctx->rt;

    const auto ms_to_samples = [&](double ms) -> unsigned int {
        if (ms <= 0.0) return 0;
        const double s = ms * static_cast<double>(rt.sample_rate()) / 1000.0;
        return static_cast<unsigned int>(std::ceil(s));
    };

    unsigned int minSamples = ms_to_samples(durationMs);
    unsigned int fadeSamples = ms_to_samples(fadeMs);
    if (minSamples == 0) minSamples = 1;
    if (fadeSamples == 0) fadeSamples = 1;

    if (!rt.speech_player_) {
        return;
    }

    // Prefer queueFrameEx if available; fall back to legacy queueFrame.
    if (rt.speechPlayer_queueFrameEx_) {
        if (!frameOrNull) {
            rt.speechPlayer_queueFrameEx_(rt.speech_player_, nullptr, nullptr, 0, minSamples, fadeSamples, userIndex, false);
            return;
        }

        runtime::nvsp_frame frame = *reinterpret_cast<const runtime::nvsp_frame*>(frameOrNull);
        rt.apply_preset_and_volume(&frame, *ctx->params);

        // frameExOrNull comes from the frontend with per-phoneme FrameEx values mixed.
        const unsigned int frameExSize = frameExOrNull ? static_cast<unsigned int>(sizeof(FrameEx)) : 0;
        rt.speechPlayer_queueFrameEx_(rt.speech_player_, &frame, frameExOrNull, frameExSize, minSamples, fadeSamples, userIndex, false);
    } else if (rt.speechPlayer_queueFrame_) {
        // Fallback: use legacy API (ignores FrameEx).
        if (!frameOrNull) {
            rt.speechPlayer_queueFrame_(rt.speech_player_, nullptr, minSamples, fadeSamples, userIndex, false);
            return;
        }

        runtime::nvsp_frame frame = *reinterpret_cast<const runtime::nvsp_frame*>(frameOrNull);
        rt.apply_preset_and_volume(&frame, *ctx->params);
        rt.speechPlayer_queueFrame_(rt.speech_player_, &frame, minSamples, fadeSamples, userIndex, false);
    }
}

std::wstring get_this_module_dir()
{
    HMODULE mod = nullptr;
    // FROM_ADDRESS: pass a pointer inside this module.
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&get_this_module_dir),
                            &mod) || !mod) {
        return L".";
    }

    wchar_t path[MAX_PATH + 1] = {};
    const DWORD n = GetModuleFileNameW(mod, path, MAX_PATH);
    if (n == 0) {
        return L".";
    }
    path[n] = L'\0';
    return strip_filename(path);
}

std::vector<std::wstring> get_installed_language_tags()
{
    const std::wstring module_dir = get_this_module_dir();
    const std::wstring base = detect_base_dir(module_dir);
    // Apply user settings (logging + language exclusions).
    const auto& settings = get_settings_cached(base);

    if (base != module_dir) {
        DEBUG_LOG("get_installed_language_tags: base_dir='%ls' (module_dir='%ls')", base.c_str(), module_dir.c_str());
    }
    const std::wstring glob = join_path(join_path(join_path(base, L"packs"), L"lang"), L"*.yaml");

    std::vector<std::wstring> tags;

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(glob.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
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
        // Reasonable fallback list (mirrors the NVDA driver defaults, plus some common locales).
        tags = {
            L"en-us",
            L"en",
            L"es",
            L"fr",
            L"de",
            L"it",
            L"ru",
            L"pl",
            L"pt-br",
            L"hu",
        };
    }

    if (!settings.excluded_lang_tags.empty()) {
        tags.erase(std::remove_if(tags.begin(), tags.end(), [&](const std::wstring& t) {
            return settings.excluded_lang_tags.find(normalize_lang_tag(t)) != settings.excluded_lang_tags.end();
        }), tags.end());
    }

    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    return tags;
}

std::wstring get_language_display_name(const std::wstring& lang_tag)
{
    const std::wstring locale = to_windows_locale_name(lang_tag);

    wchar_t buf[256] = {};
    const int n = GetLocaleInfoEx(locale.c_str(), LOCALE_SLOCALIZEDDISPLAYNAME, buf, static_cast<int>(_countof(buf)));
    if (n > 0) {
        return std::wstring(buf);
    }

    // Fall back to the tag itself.
    return lang_tag;
}

std::wstring lang_tag_to_lcid_hex(const std::wstring& lang_tag)
{
    const std::wstring locale = to_windows_locale_name(lang_tag);
    const LCID lcid = LocaleNameToLCID(locale.c_str(), 0);
    if (lcid == 0) {
        return lcid_to_hex(k_default_lcid);
    }
    return lcid_to_hex(lcid);
}

// Built-in Python presets (always available, handled by apply_preset_and_volume).
// These match the voices dict in constants.py.
const std::vector<std::wstring> k_builtin_presets = {
    L"Adam", L"Benjamin", L"Caleb", L"David", L"Robert"
};

// Prefix used for voice profile IDs to distinguish from built-in presets.
// Must match VOICE_PROFILE_PREFIX in constants.py.
const std::wstring k_voice_profile_prefix = L"profile:";

// Set to true to enable debug logging to %USERPROFILE%\TGSpeechSapi_debug.log
constexpr bool k_enable_debug_logging = false;

// Debug logging helper - writes to %USERPROFILE%\TGSpeechSapi_debug.log
// Uses only narrow strings to avoid format specifier issues
static void debug_log_to_file(const char* msg) {
    if (!k_enable_debug_logging) return;
    
    static std::string log_path;
    static bool initialized = false;
    
    if (!initialized) {
        initialized = true;
        char* userProfile = nullptr;
        size_t len = 0;
        if (_dupenv_s(&userProfile, &len, "USERPROFILE") == 0 && userProfile) {
            log_path = std::string(userProfile) + "\\TGSpeechSapi_debug.log";
            free(userProfile);
        } else {
            log_path = "C:\\TGSpeechSapi_debug.log";
        }
        
        // Clear the log file on first use
        FILE* f = fopen(log_path.c_str(), "w");
        if (f) {
            fprintf(f, "=== TGSpeechSapi Debug Log ===\n");
            fclose(f);
        }
    }
    
    FILE* f = fopen(log_path.c_str(), "a");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d.%03d] %s\n", 
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
        fclose(f);
    }
}

std::vector<std::wstring> get_voice_profile_names()
{
    // Result: built-in presets first (no prefix), then profiles (with prefix).
    std::vector<std::wstring> result;

    // Always include built-in presets.
    for (const auto& preset : k_builtin_presets) {
        result.push_back(preset);
    }

    try {
        debug_log_to_file("get_voice_profile_names: starting profile discovery");

        // Create a temporary frontend handle to query voice profiles from phonemes.yaml.
        const std::wstring module_dir = get_this_module_dir();
        const std::wstring base = detect_base_dir(module_dir);
        const std::string pack_dir_utf8 = utils::wstring_to_string(base);

        debug_log_to_file(("get_voice_profile_names: pack_dir=" + pack_dir_utf8).c_str());

        // Load the frontend DLL.
        const std::wstring frontend_dll_path = join_path(module_dir, L"nvspFrontend.dll");
        
        HMODULE frontend_mod = LoadLibraryW(frontend_dll_path.c_str());
        if (!frontend_mod) {
            DWORD err = GetLastError();
            debug_log_to_file(("get_voice_profile_names: LoadLibrary failed, error=" + std::to_string(err)).c_str());
            return result;  // Return built-ins only
        }
        debug_log_to_file("get_voice_profile_names: DLL loaded successfully");

        auto cleanup_mod = [&]() {
            if (frontend_mod) {
                FreeLibrary(frontend_mod);
                frontend_mod = nullptr;
            }
        };

        // Resolve required exports.
        using create_t = void* (__cdecl*)(const char*);
        using destroy_t = void (__cdecl*)(void*);
        using setLang_t = int (__cdecl*)(void*, const char*);
        using getNames_t = const char* (__cdecl*)(void*);

        auto fn_create = reinterpret_cast<create_t>(GetProcAddress(frontend_mod, "nvspFrontend_create"));
        auto fn_destroy = reinterpret_cast<destroy_t>(GetProcAddress(frontend_mod, "nvspFrontend_destroy"));
        auto fn_setLang = reinterpret_cast<setLang_t>(GetProcAddress(frontend_mod, "nvspFrontend_setLanguage"));
        auto fn_getNames = reinterpret_cast<getNames_t>(GetProcAddress(frontend_mod, "nvspFrontend_getVoiceProfileNames"));

        debug_log_to_file("get_voice_profile_names: exports resolved");

        if (!fn_create || !fn_destroy || !fn_setLang || !fn_getNames) {
            debug_log_to_file("get_voice_profile_names: missing exports!");
            cleanup_mod();
            return result;  // Return built-ins only
        }

        debug_log_to_file("get_voice_profile_names: calling nvspFrontend_create");
        void* frontend = fn_create(pack_dir_utf8.c_str());
        if (!frontend) {
            debug_log_to_file("get_voice_profile_names: nvspFrontend_create returned NULL");
            cleanup_mod();
            return result;  // Return built-ins only
        }
        debug_log_to_file("get_voice_profile_names: frontend created OK");

        // CRITICAL: Must call setLanguage to load the pack before querying profiles!
        // The pack (including voiceProfiles) is loaded lazily on first setLanguage call.
        debug_log_to_file("get_voice_profile_names: calling setLanguage to load pack");
        int langOk = fn_setLang(frontend, "default");
        if (!langOk) {
            debug_log_to_file("get_voice_profile_names: setLanguage failed");
            fn_destroy(frontend);
            cleanup_mod();
            return result;  // Return built-ins only
        }
        debug_log_to_file("get_voice_profile_names: setLanguage OK, pack loaded");

        debug_log_to_file("get_voice_profile_names: calling nvspFrontend_getVoiceProfileNames");
        const char* names_utf8 = fn_getNames(frontend);
        
        if (names_utf8 == nullptr) {
            debug_log_to_file("get_voice_profile_names: returned NULL");
        } else if (names_utf8[0] == '\0') {
            debug_log_to_file("get_voice_profile_names: returned empty string");
        } else {
            debug_log_to_file(("get_voice_profile_names: returned: " + std::string(names_utf8)).c_str());
        }

        if (names_utf8 && names_utf8[0] != '\0') {
            // Parse newline-separated list: "Crystal\nBeth\nBobby\n"
            std::string names(names_utf8);
            size_t start = 0;
            while (start < names.size()) {
                size_t end = names.find('\n', start);
                if (end == std::string::npos) {
                    end = names.size();
                }
                if (end > start) {
                    std::string name = names.substr(start, end - start);
                    if (!name.empty()) {
                        debug_log_to_file(("get_voice_profile_names: adding profile: " + name).c_str());
                        // Add with "profile:" prefix to distinguish from built-ins.
                        result.push_back(k_voice_profile_prefix + utils::string_to_wstring(name));
                    }
                }
                start = end + 1;
            }
        }

        debug_log_to_file(("get_voice_profile_names: total voices=" + std::to_string(result.size())).c_str());

        fn_destroy(frontend);
        cleanup_mod();
    }
    catch (...) {
        debug_log_to_file("get_voice_profile_names: EXCEPTION caught, returning built-ins only");
        // On any exception, just return built-ins
    }

    return result;
}

std::mutex& runtime::espeak_mutex()
{
    static std::mutex m;
    return m;
}

namespace {
std::unordered_set<HMODULE>& get_espeak_inited_modules()
{
    static std::unordered_set<HMODULE> mods;
    return mods;
}

// We load libespeak-ng.dll dynamically per runtime instance. The OS tracks a
// DLL refcount internally, but we also need a small per-process counter so we
// only call espeak_Terminate() when the last user goes away.
std::unordered_map<HMODULE, size_t>& get_espeak_module_refcounts()
{
    static std::unordered_map<HMODULE, size_t> refs;
    return refs;
}

void add_espeak_module_ref(HMODULE mod)
{
    if (!mod) return;
    auto& refs = get_espeak_module_refcounts();
    refs[mod] += 1;
}

// Returns true if this was the last reference.
bool release_espeak_module_ref(HMODULE mod)
{
    if (!mod) return true;
    auto& refs = get_espeak_module_refcounts();
    auto it = refs.find(mod);
    if (it == refs.end()) {
        return true;
    }
    if (it->second > 0) {
        it->second -= 1;
    }
    if (it->second == 0) {
        refs.erase(it);
        return true;
    }
    return false;
}
} // namespace

bool runtime::is_espeak_initialized(HMODULE mod)
{
    if (!mod) return false;
    const auto& mods = get_espeak_inited_modules();
    return mods.find(mod) != mods.end();
}

void runtime::mark_espeak_initialized(HMODULE mod)
{
    if (!mod) return;
    get_espeak_inited_modules().insert(mod);
}

void runtime::unmark_espeak_initialized(HMODULE mod)
{
    if (!mod) return;
    get_espeak_inited_modules().erase(mod);
}

runtime::runtime()
    : module_dir_(get_this_module_dir())
{
    base_dir_ = detect_base_dir(module_dir_);
    espeak_data_dir_ = detect_espeak_data_dir(module_dir_, base_dir_);
    // Apply user settings (logging + language exclusions).
    (void)get_settings_cached(base_dir_);


    DEBUG_LOG("runtime: module_dir='%ls' base_dir='%ls' espeak_data_dir='%ls'",
        module_dir_.c_str(), base_dir_.c_str(), espeak_data_dir_.c_str());
}

runtime::~runtime()
{
    // Destroy frontend first (it is independent, but helps keep ordering clean).
    if (nvspFrontend_destroy_ && frontend_) {
        nvspFrontend_destroy_(frontend_);
        frontend_ = nullptr;
    }

    if (speechPlayer_terminate_ && speech_player_) {
        speechPlayer_terminate_(speech_player_);
        speech_player_ = nullptr;
    }

    if (frontend_mod_) {
        FreeLibrary(frontend_mod_);
        frontend_mod_ = nullptr;
    }
    if (speech_player_mod_) {
        FreeLibrary(speech_player_mod_);
        speech_player_mod_ = nullptr;
    }
	    // eSpeak is process-global and keeps internal state. If we unload the DLL
	    // and later reload it, we must not treat it as still initialized.
	    if (espeak_mod_) {
	        {
	            std::lock_guard<std::mutex> lock(espeak_mutex());
	            const bool last_ref = release_espeak_module_ref(espeak_mod_);
	            if (last_ref && is_espeak_initialized(espeak_mod_)) {
	                if (espeak_Terminate_) {
	                    bool crashed = false;
	                    (void)safe_espeak_Terminate(espeak_Terminate_, &crashed);
	                    if (crashed) {
	                        DEBUG_LOG("~runtime: espeak_Terminate crashed");
	                    }
	                }
	                unmark_espeak_initialized(espeak_mod_);
	            }
	        }
	        FreeLibrary(espeak_mod_);
	        espeak_mod_ = nullptr;
	    }
}

HRESULT runtime::ensure_initialized()
{
    if (speech_player_ && frontend_ && espeak_mod_) {
        return S_OK;
    }

    HRESULT hr = load_modules();
    if (FAILED(hr)) return hr;

    hr = init_speech_player();
    if (FAILED(hr)) return hr;

    hr = init_frontend();
    if (FAILED(hr)) return hr;

    hr = init_espeak();
    if (FAILED(hr)) return hr;

    return S_OK;
}

HRESULT runtime::set_language(const std::wstring& lang_tag)
{
    HRESULT hr = ensure_initialized();
    if (FAILED(hr)) return hr;

    const std::wstring tag = lang_tag.empty() ? L"en-us" : lang_tag;
    const std::string tag_utf8 = utils::wstring_to_string(tag);

    // Our TGSpeechBox packs may include a synthetic "default" language tag. eSpeak doesn't.
    // Keep "default" for the frontend (it may be a real pack), but map it to a sane
    // eSpeak voice for IPA conversion.
    std::wstring espeak_tag = tag;
    if (_wcsicmp(espeak_tag.c_str(), L"default") == 0) {
        espeak_tag = L"en-us";
    }
    const std::string espeak_tag_utf8 = utils::wstring_to_string(espeak_tag);

    // Frontend language.
    if (nvspFrontend_setLanguage_ && frontend_) {
        if (!nvspFrontend_setLanguage_(frontend_, tag_utf8.c_str())) {
            const char* err = nvspFrontend_getLastError_ ? nvspFrontend_getLastError_(frontend_) : "nvspFrontend_setLanguage failed";
            (void)err;
            return E_FAIL;
        }
    }

    auto select_espeak_voice = [&](const std::string& desired) -> bool {
        std::lock_guard<std::mutex> lock(espeak_mutex());
        if (!espeak_SetVoiceByName_) {
            // If the build doesn't export voice selection, don't fail here.
            return true;
        }

        auto try_set_voice = [&](const std::string& name) -> bool {
            if (name.empty()) return false;
            if (!current_espeak_voice_.empty() && _stricmp(current_espeak_voice_.c_str(), name.c_str()) == 0) {
                return true;
            }
            bool crashed = false;
            const int rc = safe_espeak_SetVoiceByName(espeak_SetVoiceByName_, name.c_str(), &crashed);
            if (crashed || rc == k_espeak_crash_rc) {
                DEBUG_LOG("espeak_SetVoiceByName crashed for '%s'", name.c_str());
                return false;
            }
            if (rc == 0) {
                current_espeak_voice_ = name;
                return true;
            }
            DEBUG_LOG("espeak_SetVoiceByName failed for '%s' (rc=%d)", name.c_str(), rc);
            return false;
        };

        if (try_set_voice(desired)) {
            return true;
        }

        // Fallback: strip region/script (e.g. en-us -> en).
        std::string base = desired;
        const size_t pos = base.find_first_of("-_");
        if (pos != std::string::npos) {
            base.resize(pos);
        }

        if (try_set_voice(base)) {
            return true;
        }

        // Last resort.
        return try_set_voice("en");
    };

    bool voice_ok = select_espeak_voice(espeak_tag_utf8);
    if (!voice_ok) {
        // If even "en" fails, something is wrong with eSpeak state. Try a re-init once.
        DEBUG_LOG("set_language: eSpeak voice selection failed (desired='%s'), attempting reinit", espeak_tag_utf8.c_str());
        espeak_needs_reinit_ = true;

        const HRESULT es_hr = init_espeak();
        if (SUCCEEDED(es_hr)) {
            espeak_needs_reinit_ = false;
            voice_ok = select_espeak_voice(espeak_tag_utf8);
        }
    }

    if (!voice_ok) {
        DEBUG_LOG("set_language: still no usable eSpeak voice; speech may fail");
        espeak_needs_reinit_ = true;
    }

    current_lang_tag_ = tag;
    return S_OK;
}

HRESULT runtime::set_voice_profile(const std::wstring& profile_name)
{
    HRESULT hr = ensure_initialized();
    if (FAILED(hr)) return hr;

    // If profile name unchanged, skip redundant calls.
    if (current_profile_name_ == profile_name) {
        return S_OK;
    }

    // Set the voice profile in the frontend (ABI v2+).
    if (nvspFrontend_setVoiceProfile_ && frontend_) {
        const std::string name_utf8 = profile_name.empty() ? "" : utils::wstring_to_string(profile_name);
        if (!nvspFrontend_setVoiceProfile_(frontend_, name_utf8.c_str())) {
            DEBUG_LOG("set_voice_profile: nvspFrontend_setVoiceProfile failed for '%ls'", profile_name.c_str());
            // Don't fail hard - legacy profiles may not exist.
        }
    }

    current_profile_name_ = profile_name;

    // Fetch and apply VoicingTone from the profile.
    apply_voicing_tone_if_available();

    return S_OK;
}

void runtime::apply_voicing_tone_if_available()
{
    has_voicing_tone_ = false;
    memset(&cached_voicing_tone_, 0, sizeof(cached_voicing_tone_));

    // Query VoicingTone from frontend (ABI v2+).
    if (nvspFrontend_getVoicingTone_ && frontend_) {
        VoicingTone tone{};
        const int has_tone = nvspFrontend_getVoicingTone_(frontend_, &tone);
        if (has_tone) {
            cached_voicing_tone_ = tone;
            has_voicing_tone_ = true;

            // Apply to speechPlayer DSP (if supported).
            if (speechPlayer_setVoicingTone_ && speech_player_) {
                speechPlayer_setVoicingTone_(speech_player_, &tone);
                DEBUG_LOG("apply_voicing_tone_if_available: applied VoicingTone (tilt=%.2f)", tone.voicedTiltDbPerOct);
            }
        }
    }
}


HRESULT runtime::queue_text(const std::wstring& text, const speak_params& params)
{
    if (text.empty()) {
        return S_OK;
    }

    HRESULT hr = ensure_initialized();
    if (FAILED(hr)) return hr;

    // Determine if this is a built-in preset or a voice profile.
    // Voice profiles use "profile:" prefix (e.g., "profile:Bobby").
    const std::wstring profilePrefix = L"profile:";
    const bool isProfile = params.preset_name.rfind(profilePrefix, 0) == 0;

    if (isProfile) {
        // It's a voice profile - extract name without prefix and set in frontend.
        std::wstring profileName = params.preset_name.substr(profilePrefix.length());
        if (profileName != current_profile_name_) {
            (void)set_voice_profile(profileName);
        }
    } else {
        // It's a built-in preset - clear any active profile so apply_preset_and_volume handles it.
        if (!current_profile_name_.empty()) {
            // Clear the profile in frontend.
            if (nvspFrontend_setVoiceProfile_ && frontend_) {
                nvspFrontend_setVoiceProfile_(frontend_, "");
            }
            current_profile_name_.clear();
            has_voicing_tone_ = false;
        }
    }

    // Reuse an internal buffer to avoid per-utterance allocations.
    ipa_buf_.clear();
    text_to_ipa_utf8(text, ipa_buf_);
    if (ipa_buf_.empty()) {
        // Nothing to queue (eSpeak may return empty for whitespace / symbols).
        return S_OK;
    }

    if (!frontend_) {
        return E_FAIL;
    }

    char clause[2] = { params.clause_type ? params.clause_type : '.', 0 };

    frame_queue_ctx ctx;
    ctx.rt = this;
    ctx.params = &params;

    int ok = 0;

    // Prefer extended API (ABI v2+) for FrameEx support.
    if (nvspFrontend_queueIPA_Ex_) {
        ok = nvspFrontend_queueIPA_Ex_(
            frontend_,
            ipa_buf_.c_str(),
            params.speed,
            params.base_pitch,
            params.inflection,
            clause,
            params.user_index_base,
            &runtime::frontend_frame_ex_cb,
            &ctx
        );
    } else if (nvspFrontend_queueIPA_) {
        // Fallback to legacy API.
        ok = nvspFrontend_queueIPA_(
            frontend_,
            ipa_buf_.c_str(),
            params.speed,
            params.base_pitch,
            params.inflection,
            clause,
            params.user_index_base,
            &runtime::frontend_frame_cb,
            &ctx
        );
    } else {
        return E_FAIL;
    }

    if (!ok) {
        const char* err = nvspFrontend_getLastError_ ? nvspFrontend_getLastError_(frontend_) : "nvspFrontend_queueIPA failed";
        (void)err;
        return E_FAIL;
    }

    return S_OK;
}

int runtime::synthesize(int max_samples, sample_t* out_samples)
{
    if (!speechPlayer_synthesize_ || !speech_player_ || max_samples <= 0 || !out_samples) {
        return 0;
    }

    return speechPlayer_synthesize_(speech_player_, static_cast<unsigned int>(max_samples), out_samples);
}

void runtime::purge()
{
    if (!speechPlayer_queueFrame_ || !speech_player_) {
        return;
    }

    const auto ms_to_samples = [&](double ms) -> unsigned int {
        if (ms <= 0.0) return 0;
        const double s = ms * static_cast<double>(sample_rate_) / 1000.0;
        return static_cast<unsigned int>(std::ceil(s));
    };

    // Match NVDA driver's cancel() defaults.
    speechPlayer_queueFrame_(speech_player_, nullptr, ms_to_samples(20.0), ms_to_samples(5.0), 0, true);
}

HRESULT runtime::load_modules()
{
    if (speech_player_mod_ && frontend_mod_ && espeak_mod_) {
        return S_OK;
    }

    module_dir_ = get_this_module_dir();

    auto load_from_dir = [&](const wchar_t* dll_name) -> HMODULE {
        const std::wstring full = join_path(module_dir_, dll_name);
        return LoadLibraryW(full.c_str());
    };

    // Required runtime dependencies.
    if (!speech_player_mod_) {
        speech_player_mod_ = load_from_dir(L"speechPlayer.dll");
        if (!speech_player_mod_) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }
    if (!frontend_mod_) {
        frontend_mod_ = load_from_dir(L"nvspFrontend.dll");
        if (!frontend_mod_) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }
    if (!espeak_mod_) {
        espeak_mod_ = load_from_dir(L"libespeak-ng.dll");
        if (!espeak_mod_) {
            // Try some common alternative names.
            espeak_mod_ = load_from_dir(L"espeak-ng.dll");
        }
        if (!espeak_mod_) {
            espeak_mod_ = load_from_dir(L"espeak.dll");
        }
        if (!espeak_mod_) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    // Resolve speechPlayer exports.
    speechPlayer_initialize_ = reinterpret_cast<speechPlayer_initialize_t>(GetProcAddress(speech_player_mod_, "speechPlayer_initialize"));
    speechPlayer_queueFrame_ = reinterpret_cast<speechPlayer_queueFrame_t>(GetProcAddress(speech_player_mod_, "speechPlayer_queueFrame"));
    speechPlayer_synthesize_ = reinterpret_cast<speechPlayer_synthesize_t>(GetProcAddress(speech_player_mod_, "speechPlayer_synthesize"));
    speechPlayer_getLastIndex_ = reinterpret_cast<speechPlayer_getLastIndex_t>(GetProcAddress(speech_player_mod_, "speechPlayer_getLastIndex"));
    speechPlayer_terminate_ = reinterpret_cast<speechPlayer_terminate_t>(GetProcAddress(speech_player_mod_, "speechPlayer_terminate"));

    if (!speechPlayer_initialize_ || !speechPlayer_queueFrame_ || !speechPlayer_synthesize_ || !speechPlayer_terminate_) {
        return E_FAIL;
    }

    // Resolve extended speechPlayer exports (DSP v5+). Optional - don't fail if missing.
    speechPlayer_queueFrameEx_ = reinterpret_cast<speechPlayer_queueFrameEx_t>(GetProcAddress(speech_player_mod_, "speechPlayer_queueFrameEx"));
    speechPlayer_setVoicingTone_ = reinterpret_cast<speechPlayer_setVoicingTone_t>(GetProcAddress(speech_player_mod_, "speechPlayer_setVoicingTone"));
    speechPlayer_getVoicingTone_ = reinterpret_cast<speechPlayer_getVoicingTone_t>(GetProcAddress(speech_player_mod_, "speechPlayer_getVoicingTone"));
    speechPlayer_getDspVersion_ = reinterpret_cast<speechPlayer_getDspVersion_t>(GetProcAddress(speech_player_mod_, "speechPlayer_getDspVersion"));

    if (speechPlayer_getDspVersion_) {
        DEBUG_LOG("speechPlayer DSP version: %u", speechPlayer_getDspVersion_());
    }

    // Resolve nvspFrontend exports.
    nvspFrontend_create_ = reinterpret_cast<nvspFrontend_create_t>(GetProcAddress(frontend_mod_, "nvspFrontend_create"));
    nvspFrontend_destroy_ = reinterpret_cast<nvspFrontend_destroy_t>(GetProcAddress(frontend_mod_, "nvspFrontend_destroy"));
    nvspFrontend_setLanguage_ = reinterpret_cast<nvspFrontend_setLanguage_t>(GetProcAddress(frontend_mod_, "nvspFrontend_setLanguage"));
    nvspFrontend_queueIPA_ = reinterpret_cast<nvspFrontend_queueIPA_t>(GetProcAddress(frontend_mod_, "nvspFrontend_queueIPA"));
    nvspFrontend_getLastError_ = reinterpret_cast<nvspFrontend_getLastError_t>(GetProcAddress(frontend_mod_, "nvspFrontend_getLastError"));

    if (!nvspFrontend_create_ || !nvspFrontend_destroy_ || !nvspFrontend_setLanguage_ || !nvspFrontend_queueIPA_) {
        return E_FAIL;
    }

    // Resolve extended nvspFrontend exports (ABI v2+). Optional - don't fail if missing.
    nvspFrontend_queueIPA_Ex_ = reinterpret_cast<nvspFrontend_queueIPA_Ex_t>(GetProcAddress(frontend_mod_, "nvspFrontend_queueIPA_Ex"));
    nvspFrontend_setVoiceProfile_ = reinterpret_cast<nvspFrontend_setVoiceProfile_t>(GetProcAddress(frontend_mod_, "nvspFrontend_setVoiceProfile"));
    nvspFrontend_getVoiceProfile_ = reinterpret_cast<nvspFrontend_getVoiceProfile_t>(GetProcAddress(frontend_mod_, "nvspFrontend_getVoiceProfile"));
    nvspFrontend_getVoiceProfileNames_ = reinterpret_cast<nvspFrontend_getVoiceProfileNames_t>(GetProcAddress(frontend_mod_, "nvspFrontend_getVoiceProfileNames"));
    nvspFrontend_getVoicingTone_ = reinterpret_cast<nvspFrontend_getVoicingTone_t>(GetProcAddress(frontend_mod_, "nvspFrontend_getVoicingTone"));
    nvspFrontend_setFrameExDefaults_ = reinterpret_cast<nvspFrontend_setFrameExDefaults_t>(GetProcAddress(frontend_mod_, "nvspFrontend_setFrameExDefaults"));
    nvspFrontend_getABIVersion_ = reinterpret_cast<nvspFrontend_getABIVersion_t>(GetProcAddress(frontend_mod_, "nvspFrontend_getABIVersion"));

    if (nvspFrontend_getABIVersion_) {
        DEBUG_LOG("nvspFrontend ABI version: %d", nvspFrontend_getABIVersion_());
    }

    // Resolve eSpeak exports.
    espeak_Initialize_ = reinterpret_cast<espeak_Initialize_t>(GetProcAddress(espeak_mod_, "espeak_Initialize"));
    espeak_SetVoiceByName_ = reinterpret_cast<espeak_SetVoiceByName_t>(GetProcAddress(espeak_mod_, "espeak_SetVoiceByName"));
    espeak_TextToPhonemes_ = reinterpret_cast<espeak_TextToPhonemes_t>(GetProcAddress(espeak_mod_, "espeak_TextToPhonemes"));
    espeak_Terminate_ = reinterpret_cast<espeak_Terminate_t>(GetProcAddress(espeak_mod_, "espeak_Terminate"));
    espeak_Info_ = reinterpret_cast<espeak_Info_t>(GetProcAddress(espeak_mod_, "espeak_Info"));

    if (!espeak_Initialize_ || !espeak_SetVoiceByName_ || !espeak_TextToPhonemes_) {
        DEBUG_LOG("init_dll_exports: required eSpeak exports missing");
        return E_FAIL;
    }

    if (!espeak_Terminate_) {
        DEBUG_LOG("init_dll_exports: optional eSpeak export missing: espeak_Terminate");
    }
    if (!espeak_Info_) {
        DEBUG_LOG("init_dll_exports: optional eSpeak export missing: espeak_Info");
    }

    return S_OK;
}

HRESULT runtime::init_speech_player()
{
    if (speech_player_) {
        return S_OK;
    }

    if (!speechPlayer_initialize_) {
        return E_FAIL;
    }

    speech_player_ = speechPlayer_initialize_(sample_rate_);
    if (!speech_player_) {
        return E_FAIL;
    }
    return S_OK;
}

HRESULT runtime::init_frontend()
{
    if (frontend_) {
        return S_OK;
    }

    if (!nvspFrontend_create_) {
        return E_FAIL;
    }

    // packDir is the directory that CONTAINS the "packs" folder.
    const std::wstring pack_base = base_dir_.empty() ? module_dir_ : base_dir_;
    const std::string pack_dir_utf8 = utils::wstring_to_string(pack_base);
    frontend_ = nvspFrontend_create_(pack_dir_utf8.c_str());
    if (!frontend_) {
        DEBUG_LOG("nvspFrontend_create failed. pack_base='%ls'", pack_base.c_str());
        return E_FAIL;
    }

    return S_OK;
}

HRESULT runtime::init_espeak()
{
    if (!espeak_mod_) {
        // module_dir_ is stored as std::wstring. Use std::filesystem::path for safe path joining.
        const std::filesystem::path espeak_dll_path = std::filesystem::path(module_dir_) / L"libespeak-ng.dll";
        espeak_mod_ = LoadLibraryExW(espeak_dll_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!espeak_mod_) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        DEBUG_LOG("init_espeak: loaded eSpeak DLL: %ls", espeak_dll_path.c_str());
	        // Track this runtime instance's reference so we can safely call
	        // espeak_Terminate() only when the last instance is destroyed.
	        {
	            std::lock_guard<std::mutex> lock(espeak_mutex());
	            add_espeak_module_ref(espeak_mod_);
	        }
    }

    if (!espeak_Initialize_) {
        espeak_Initialize_ = reinterpret_cast<espeak_Initialize_t>(GetProcAddress(espeak_mod_, "espeak_Initialize"));
    }
    if (!espeak_SetVoiceByName_) {
        espeak_SetVoiceByName_ = reinterpret_cast<espeak_SetVoiceByName_t>(GetProcAddress(espeak_mod_, "espeak_SetVoiceByName"));
    }
    if (!espeak_TextToPhonemes_) {
        espeak_TextToPhonemes_ = reinterpret_cast<espeak_TextToPhonemes_t>(GetProcAddress(espeak_mod_, "espeak_TextToPhonemes"));
    }
    if (!espeak_Terminate_) {
        espeak_Terminate_ = reinterpret_cast<espeak_Terminate_t>(GetProcAddress(espeak_mod_, "espeak_Terminate"));
    }
    if (!espeak_Info_) {
        espeak_Info_ = reinterpret_cast<espeak_Info_t>(GetProcAddress(espeak_mod_, "espeak_Info"));
    }
    if (!espeak_ng_InitializePath_) {
        espeak_ng_InitializePath_ = reinterpret_cast<espeak_ng_InitializePath_t>(GetProcAddress(espeak_mod_, "espeak_ng_InitializePath"));
    }

    if (!espeak_Initialize_ || !espeak_TextToPhonemes_) {
        return E_FAIL;
    }

    std::lock_guard lock(espeak_mutex());

    auto health_check = [this]() -> bool {
        if (!espeak_SetVoiceByName_ || !espeak_TextToPhonemes_) {
            return false;
        }

        bool voice_crashed = false;
        const int rc = safe_espeak_SetVoiceByName(espeak_SetVoiceByName_, "en", &voice_crashed);
        if (voice_crashed || rc != 0) {
            return false;
        }

        bool phon_crashed = false;
        const wchar_t* probe = L"test";
        const void* text_ptr = probe;
        const char* out = safe_espeak_TextToPhonemes(
            espeak_TextToPhonemes_,
            &text_ptr,
            k_espeak_chars_wchar,
            k_espeak_phoneme_mode_ipa,
            &phon_crashed);

        return !phon_crashed && out && *out;
    };

    if (is_espeak_initialized(espeak_mod_)) {
        if (health_check()) {
            return S_OK;
        }

        DEBUG_LOG("init_espeak: cached init failed health check; reinitializing");
        bool term_crashed = false;
        (void)safe_espeak_Terminate(espeak_Terminate_, &term_crashed);
        if (term_crashed) {
            DEBUG_LOG("init_espeak: espeak_Terminate crashed");
        }
        // fall through and attempt full initialization below
    }

    const auto base_utf8 = wide_to_utf8(base_dir_);

    auto try_init = [this, &base_utf8](const char* label, const std::filesystem::path& init_path) -> HRESULT {
        const auto init_utf8 = wide_to_utf8(init_path);
        DEBUG_LOG("espeak_Initialize attempt (%s): init_path='%s'", label, init_utf8.c_str());

        if (espeak_ng_InitializePath_) {
            espeak_ng_InitializePath_(base_utf8.c_str());
        }

        const int sr = espeak_Initialize_(
            1 /* espeakAUDIO_OUTPUT_RETRIEVAL */,
            0 /* buflength */,
            init_utf8.c_str(),
            0 /* options */);

        DEBUG_LOG("espeak_Initialize returned sr=%d", sr);
        if (sr <= 0) {
            return E_FAIL;
        }

        const char* data_path = nullptr;
        const char* version = nullptr;
        if (espeak_Info_) {
            version = espeak_Info_(&data_path);
        }
        if (version || data_path) {
            DEBUG_LOG("espeak_Info: version='%s' data_path='%s'", version ? version : "", data_path ? data_path : "");
        }

        // Smoke test voice + phonemes
        if (espeak_SetVoiceByName_) {
            bool crashed = false;
            const int rc = safe_espeak_SetVoiceByName(espeak_SetVoiceByName_, "en", &crashed);
            if (crashed || rc != 0) {
                DEBUG_LOG("espeak_SetVoiceByName smoke test failed for 'en' (rc=%d%s)", rc, crashed ? ", crashed" : "");
                return E_FAIL;
            }
        }

        bool crashed = false;
        const wchar_t* test = L"test";
        const void* ptr = test;
        const char* out = safe_espeak_TextToPhonemes(
            espeak_TextToPhonemes_,
            &ptr,
            k_espeak_chars_wchar,
            k_espeak_phoneme_mode_ipa,
            &crashed);
        if (crashed || !out) {
            DEBUG_LOG("espeak_TextToPhonemes smoke test crashed or returned null");
            return E_FAIL;
        }

        return S_OK;
    };

	    // Prefer the explicit data directory first. Passing the parent directory
	    // (base_dir_) can return a sample rate but later fail to load voices.
	    auto hr = try_init("data_dir", espeak_data_dir_);
	    if (FAILED(hr)) {
	        DEBUG_LOG("espeak init failed using espeak_data_dir, trying base_dir");
	        bool term_crashed = false;
	        (void)safe_espeak_Terminate(espeak_Terminate_, &term_crashed);
	        if (term_crashed) {
	            DEBUG_LOG("espeak_Terminate crashed");
	        }
	        hr = try_init("data_home", base_dir_);
	    }

    if (SUCCEEDED(hr)) {
        mark_espeak_initialized(espeak_mod_);
        espeak_needs_reinit_ = false;
    }

    return hr;
}



void runtime::text_to_ipa_utf8(const std::wstring& text, std::string& out_ipa)
{
    out_ipa.clear();
    if (text.empty()) {
        return;
    }

    // If eSpeak has crashed repeatedly in a short window, avoid a tight
    // reinit/retry loop (LoadLibrary/FreeLibrary is expensive).
    const auto now = std::chrono::steady_clock::now();
    if (now < espeak_disable_until_) {
        return;
    }

    auto note_crash = [&]() {
        const auto t = std::chrono::steady_clock::now();
        if (espeak_crash_streak_ > 0 && (t - espeak_last_crash_) < std::chrono::seconds(1)) {
            ++espeak_crash_streak_;
        } else {
            espeak_crash_streak_ = 1;
        }
        espeak_last_crash_ = t;
        if (espeak_crash_streak_ >= 2) {
            espeak_disable_until_ = t + std::chrono::seconds(5);
            DEBUG_LOG("text_to_ipa_utf8: repeated eSpeak crashes; backing off for 5s");
        }
    };

    auto trim_ascii_whitespace = [&](std::string& s) {
        auto is_ws = [](char c) {
            return c == ' ' || c == '\n' || c == '\r' || c == '\t';
        };
        // Trim leading whitespace (erase once).
        size_t start = 0;
        while (start < s.size() && is_ws(s[start])) {
            ++start;
        }
        if (start > 0) {
            s.erase(0, start);
        }
        // Trim trailing whitespace.
        while (!s.empty() && is_ws(s.back())) {
            s.pop_back();
        }
    };

    // If a previous call crashed, try to recover once before attempting conversion.
    if (espeak_needs_reinit_) {
        DEBUG_LOG("text_to_ipa_utf8: reinitializing eSpeak after previous failure");
        const HRESULT hr = init_espeak();
        if (FAILED(hr)) {
            return;
        }
        espeak_needs_reinit_ = false;
        if (!current_lang_tag_.empty()) {
            (void)set_language(current_lang_tag_);
        }
    }

    // Ensure a language is set (so phoneme generation is consistent).
    if (current_lang_tag_.empty()) {
        (void)set_language(L"en-us");
    }

    auto convert_once = [&](const std::wstring& t, bool* out_crashed) {
        if (out_crashed) {
            *out_crashed = false;
        }
        if (!espeak_TextToPhonemes_) {
            return;
        }

        // Pre-reserve to reduce reallocations (IPA often expands vs input).
        const size_t want = t.size() * 4;
        if (out_ipa.capacity() < want) {
            out_ipa.reserve(want);
        }

        // eSpeak is not thread-safe.
        std::lock_guard<std::mutex> lock(espeak_mutex());

        const void* text_ptr = static_cast<const void*>(t.c_str());
        while (text_ptr) {
            const auto* w = reinterpret_cast<const wchar_t*>(text_ptr);
            if (!w || *w == L'\0') {
                break;
            }

            bool crashed = false;
            const char* phon = safe_espeak_TextToPhonemes(
                espeak_TextToPhonemes_,
                &text_ptr,
                k_espeak_chars_wchar,
                k_espeak_phoneme_mode_ipa,
                &crashed);

            if (crashed) {
                espeak_needs_reinit_ = true;
                if (out_crashed) {
                    *out_crashed = true;
                }
                out_ipa.clear();
                return;
            }
            if (!phon) {
                break;
            }
            out_ipa.append(phon);
        }
    };

    // First attempt.
    bool crashed = false;
    convert_once(text, &crashed);
    if (!crashed) {
        // Successful call (even if output is empty) -> reset crash streak.
        espeak_crash_streak_ = 0;
        trim_ascii_whitespace(out_ipa);
        return;
    }

    DEBUG_LOG("espeak_TextToPhonemes crashed (len=%zu)", text.size());
    note_crash();

    // Back off if we're in a repeated-crash scenario.
    if (std::chrono::steady_clock::now() < espeak_disable_until_) {
        return;
    }

    // Reinitialize and retry once.
    DEBUG_LOG("text_to_ipa_utf8: retrying after reinit");
    {
        const HRESULT hr = init_espeak();
        if (FAILED(hr)) {
            return;
        }
        espeak_needs_reinit_ = false;
        if (!current_lang_tag_.empty()) {
            (void)set_language(current_lang_tag_);
        }
    }

    out_ipa.clear();
    crashed = false;
    convert_once(text, &crashed);
    if (crashed) {
        DEBUG_LOG("espeak_TextToPhonemes crashed again after reinit (len=%zu)", text.size());
        note_crash();
        return;
    }

    // Success after retry.
    espeak_crash_streak_ = 0;
    trim_ascii_whitespace(out_ipa);
}

void runtime::apply_preset_and_volume(void* frame_ptr, const speak_params& params)
{
    if (!frame_ptr) {
        return;
    }

    nvsp_frame& f = *reinterpret_cast<nvsp_frame*>(frame_ptr);

    // Field indices in the ABI frame layout (must match speechPlayer_frame_t).
    constexpr int I_voicePitch = 0;
    constexpr int I_vibratoPitchOffset = 1;
    constexpr int I_vibratoSpeed = 2;
    constexpr int I_voiceTurbulenceAmplitude = 3;
    constexpr int I_glottalOpenQuotient = 4;
    constexpr int I_voiceAmplitude = 5;
    constexpr int I_aspirationAmplitude = 6;
    constexpr int I_cf1 = 7;
    constexpr int I_cf2 = 8;
    constexpr int I_cf3 = 9;
    constexpr int I_cf4 = 10;
    constexpr int I_cf5 = 11;
    constexpr int I_cf6 = 12;
    constexpr int I_cfNP = 14;
    constexpr int I_cb1 = 15;
    constexpr int I_cb2 = 16;
    constexpr int I_cb3 = 17;
    constexpr int I_cb4 = 18;
    constexpr int I_cb5 = 19;
    constexpr int I_cb6 = 20;
    constexpr int I_fricationAmplitude = 24;
    constexpr int I_pf3 = 27;
    constexpr int I_pf4 = 28;
    constexpr int I_pf5 = 29;
    constexpr int I_pf6 = 30;
    constexpr int I_pb1 = 31;
    constexpr int I_pb2 = 32;
    constexpr int I_pb3 = 33;
    constexpr int I_pb4 = 34;
    constexpr int I_pb5 = 35;
    constexpr int I_pb6 = 36;
    constexpr int I_pa3 = 39;
    constexpr int I_pa4 = 40;
    constexpr int I_pa5 = 41;
    constexpr int I_pa6 = 42;
    constexpr int I_parallelBypass = 43;
    constexpr int I_preFormantGain = 44;
    constexpr int I_outputGain = 45;
    constexpr int I_endVoicePitch = 46;

    // Voice preset modifications (ported from TGSpeechBox's NVDA driver constants.py).
    auto preset = params.preset_name;
    // Normalize preset name comparison (case-insensitive).
    auto eq = [](const std::wstring& a, const wchar_t* b) {
        return _wcsicmp(a.c_str(), b) == 0;
    };

    if (eq(preset, L"Adam") || preset.empty()) {
        f.fields[I_cb1] *= 1.3;
        f.fields[I_pa6] *= 1.3;
        f.fields[I_fricationAmplitude] *= 0.85;
    }
    else if (eq(preset, L"Benjamin")) {
        f.fields[I_cf1] *= 1.01;
        f.fields[I_cf2] *= 1.02;
        f.fields[I_cf4] = 3770;
        f.fields[I_cf5] = 4100;
        f.fields[I_cf6] = 5000;
        f.fields[I_cfNP] *= 0.9;
        f.fields[I_cb1] *= 1.3;
        f.fields[I_fricationAmplitude] *= 0.7;
        f.fields[I_pa6] *= 1.3;
    }
    else if (eq(preset, L"Caleb")) {
        f.fields[I_aspirationAmplitude] = 1;
        f.fields[I_voiceAmplitude] = 0;
    }
    else if (eq(preset, L"David")) {
        f.fields[I_voicePitch] *= 0.75;
        f.fields[I_endVoicePitch] *= 0.75;
        f.fields[I_cf1] *= 0.75;
        f.fields[I_cf2] *= 0.85;
        f.fields[I_cf3] *= 0.85;
    }
    else if (eq(preset, L"Robert")) {
        // Eloquence-inspired voice: bright, crisp, synthetic
        // Pitch
        f.fields[I_voicePitch] *= 1.10;
        f.fields[I_endVoicePitch] *= 1.10;
        // Cascade formants
        f.fields[I_cf1] *= 1.02;
        f.fields[I_cf2] *= 1.06;
        f.fields[I_cf3] *= 1.08;
        f.fields[I_cf4] *= 1.08;
        f.fields[I_cf5] *= 1.10;
        f.fields[I_cf6] *= 1.05;
        // Narrow bandwidths for buzzy synthetic sound
        f.fields[I_cb1] *= 0.65;
        f.fields[I_cb2] *= 0.68;
        f.fields[I_cb3] *= 0.72;
        f.fields[I_cb4] *= 0.75;
        f.fields[I_cb5] *= 0.78;
        f.fields[I_cb6] *= 0.80;
        // Pressed glottis
        f.fields[I_glottalOpenQuotient] = 0.30;
        // Minimal breathiness
        f.fields[I_voiceTurbulenceAmplitude] *= 0.20;
        // Frication for consonant clarity
        f.fields[I_fricationAmplitude] *= 0.75;
        // Parallel bypass
        f.fields[I_parallelBypass] *= 0.70;
        // Parallel formant amplitudes
        f.fields[I_pa3] *= 1.08;
        f.fields[I_pa4] *= 1.15;
        f.fields[I_pa5] *= 1.20;
        f.fields[I_pa6] *= 1.25;
        // Parallel bandwidths
        f.fields[I_pb1] *= 0.72;
        f.fields[I_pb2] *= 0.75;
        f.fields[I_pb3] *= 0.78;
        f.fields[I_pb4] *= 0.80;
        f.fields[I_pb5] *= 0.82;
        f.fields[I_pb6] *= 0.85;
        // Parallel formant frequencies
        f.fields[I_pf3] *= 1.06;
        f.fields[I_pf4] *= 1.08;
        f.fields[I_pf5] *= 1.10;
        f.fields[I_pf6] *= 1.00;
        // No vibrato
        f.fields[I_vibratoPitchOffset] = 0.0;
        f.fields[I_vibratoSpeed] = 0.0;
        // Note: voicedTiltDbPerOct (-6.0) is a VoicingTone param, not handled here.
        // For full Robert experience, would need to set VoicingTone separately.
    }

    // Volume scaling.
    //
    // In practice, TGSpeechBox can sound a bit quiet at "100%" compared to
    // some other SAPI voices. Keep the 0..1 mapping, but apply a small extra
    // boost at the *output* stage only, so we do not over-drive the earlier
    // formant stage.
    double v = params.volume;
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;

    // Keep pre-formant gain linear.
    f.fields[I_preFormantGain] *= v;

    // Give output gain a gentle lift near the top.
    // At v=1.0 this becomes 1.25 (i.e. +25%).
    constexpr double k_output_gain_boost_at_max = 0.95;
    const double out_v = v * (1.0 + k_output_gain_boost_at_max * v);
    f.fields[I_outputGain] *= out_v;
}

} // namespace tgsb
} // namespace TGSpeech
