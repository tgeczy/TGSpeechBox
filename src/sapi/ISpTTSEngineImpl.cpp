/*
TGSpeechBox — SAPI ISpTTSEngineSite TTS engine implementation.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "ISpTTSEngineImpl.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <new>
#include <string>
#include <vector>

#include <sperror.h>

#include "utils.hpp"
#include "debug_log.h"
#include "tgsb_settings.hpp"
#include "nvspFrontend.h"

namespace TGSpeech {
namespace sapi {

namespace {

constexpr WORD k_audio_channels = 1;
constexpr DWORD k_default_audio_sample_rate = 22050;
constexpr WORD k_audio_bits_per_sample = 16;

constexpr double k_default_inflection = 0.55;

// The highest speed the frontend is asked for; above it the DSP's time
// stretch takes over (the same 2.0 on NVDA, Android, iOS and Linux).
constexpr double k_synth_speed_cap = 2.0;

// What "Rate boost" multiplies the speed by: 2, as the NVDA driver does
// (speech_pipeline.py); tests/test_sapi_rate_boost.py holds the two together.
constexpr double k_rate_boost = 2.0;

struct speak_context {
    ISpTTSEngineSite* site = nullptr;
    ULONGLONG bytes_written = 0;
    bool aborted = false;
};

bool write_bytes(ISpTTSEngineSite* site, const BYTE* data, ULONG bytes, ULONGLONG& inout_bytes_written)
{
    if (!site || !data) {
        return false;
    }

    ULONG remaining = bytes;
    const BYTE* ptr = data;

    while (remaining > 0) {
        ULONG written = 0;
        const HRESULT hr = site->Write(ptr, remaining, &written);
        if (FAILED(hr)) {
            DEBUG_LOG("TGSpeechSapi: Write failed HRESULT=0x%08X", hr);
            return false;
        }
        if (written == 0) {
            // Win7 SAPI returns 0 when the audio buffer is full.
            // Return true (not fatal) — caller should Sleep + check abort + retry.
            return true;
        }
        if (written > remaining) {
            DEBUG_LOG("TGSpeechSapi: Write overrun (written=%lu, remaining=%lu)", written, remaining);
            return false;
        }
        inout_bytes_written += written;
        remaining -= written;
        ptr += written;
    }

    return true;
}

char detect_clause_type(const wchar_t* start, size_t len)
{
    // Scan backward, skipping whitespace and closing quotes/brackets,
    // then return the clause-ending punctuation if found.
    for (size_t i = len; i > 0; --i) {
        const wchar_t c = start[i - 1];
        // Skip whitespace.
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n')
            continue;
        // Skip closing quotes and brackets.
        if (c == L')' || c == L']' || c == L'"' || c == L'\'' ||
            c == L'\u2019' || c == L'\u201D')
            continue;
        if (c == L'.') return '.';
        if (c == L',') return ',';
        if (c == L'?') return '?';
        if (c == L'!') return '!';
        break;
    }
    return '.';
}

// Pad emoji codepoints with spaces so eSpeak treats them as separate
// words for $textmode dictionary lookup.  On Windows, wchar_t is UTF-16
// so emoji above U+FFFF are surrogate pairs.
static std::wstring padEmojiWithSpacesW(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size() * 2);
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        // Detect surrogate pair (emoji above U+FFFF)
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size()) {
            wchar_t lo = text[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                uint32_t cp = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                bool isEmoji = (cp >= 0x1F000 && cp <= 0x1FBFF) ||
                               (cp >= 0x1F1E0 && cp <= 0x1F1FF);
                if (isEmoji) {
                    if (!out.empty() && out.back() != L' ') out += L' ';
                    out += c; out += lo;
                    i++;
                    // Skip variation selectors (U+FE0E/FE0F)
                    while (i + 1 < text.size() &&
                           (text[i + 1] == 0xFE0E || text[i + 1] == 0xFE0F))
                        out += text[++i];
                    if (i + 1 < text.size() && text[i + 1] != L' ') out += L' ';
                    continue;
                }
                // Non-emoji surrogate pair — pass through
                out += c; out += lo;
                i++;
                continue;
            }
        }
        // BMP emoji: U+2600..U+27BF (Misc Symbols + Dingbats)
        if (c >= 0x2600 && c <= 0x27BF) {
            if (!out.empty() && out.back() != L' ') out += L' ';
            out += c;
            // Skip variation selectors
            while (i + 1 < text.size() &&
                   (text[i + 1] == 0xFE0E || text[i + 1] == 0xFE0F))
                out += text[++i];
            if (i + 1 < text.size() && text[i + 1] != L' ') out += L' ';
            continue;
        }
        out += c;
    }
    return out;
}

// Split a SAPI text fragment into clauses with the frontend's splitter, the
// one every TGSpeechBox host uses (#133), so SAPI pauses where NVDA, Android,
// iOS and Linux do.  Offsets are in the fragment's UTF-16 units.
struct sapi_clause {
    size_t start;
    size_t len;
    char   clause_type;
    double pause_ms;
};

std::vector<sapi_clause> split_clauses(const std::wstring& text, int pause_mode)
{
    // A UTF-8 copy, and for every byte of it the UTF-16 index it came from.
    std::string utf8;
    std::vector<size_t> w_at;
    utf8.reserve(text.size() * 2);
    w_at.reserve(text.size() * 2 + 1);
    for (size_t i = 0; i < text.size();) {
        uint32_t cp = text[i];
        size_t units = 1;
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size() &&
            text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            units = 2;
        }
        char buf[4];
        size_t n = 0;
        if (cp < 0x80) {
            buf[n++] = static_cast<char>(cp);
        } else if (cp < 0x800) {
            buf[n++] = static_cast<char>(0xC0 | (cp >> 6));
            buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            buf[n++] = static_cast<char>(0xE0 | (cp >> 12));
            buf[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            buf[n++] = static_cast<char>(0xF0 | (cp >> 18));
            buf[n++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            buf[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            buf[n++] = static_cast<char>(0x80 | (cp & 0x3F));
        }
        for (size_t k = 0; k < n; ++k) {
            utf8.push_back(buf[k]);
            w_at.push_back(i);
        }
        i += units;
    }
    w_at.push_back(text.size());

    std::vector<sapi_clause> clauses;
    const int len = static_cast<int>(utf8.size());
    int pos = 0;
    while (pos < len) {
        int start = 0, end = 0;
        char type = '.';
        double pause = 0.0;
        const int next = nvspFrontend_nextClause(utf8.c_str(), len, pos, pause_mode,
                                                 &start, &end, &type, &pause);
        if (next <= pos) break;
        if (end > start)
            clauses.push_back({ w_at[start], w_at[end] - w_at[start], type, pause });
        pos = next;
    }
    return clauses;
}

// A spelled fragment (SPVA_SpellOut, from <spell>) is read character by
// character (#207).  Each character is a text of its own, as a lone character
// is in character navigation, so the frontend names it from the language's
// letter dictionary or leaves the name to the phonemizer.  One entry per
// character, white space left out; a surrogate pair is one character, and a
// variation selector or a combining mark stays with the character before it.
std::vector<sapi_clause> spell_clauses(const std::wstring& text)
{
    std::vector<sapi_clause> clauses;
    for (size_t i = 0; i < text.size();) {
        size_t units = 1;
        if (text[i] >= 0xD800 && text[i] <= 0xDBFF && i + 1 < text.size() &&
            text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            units = 2;
        } else if (std::iswspace(text[i])) {
            ++i;
            continue;
        }
        size_t end = i + units;
        while (end < text.size() && (text[end] == 0xFE0E || text[end] == 0xFE0F ||
                                     (text[end] >= 0x0300 && text[end] <= 0x036F)))
            ++end;
        // Read as the items of a list: each one rises, the last one falls.
        clauses.push_back({ i, end - i, ',', 0.0 });
        i = end;
    }
    if (!clauses.empty()) clauses.back().clause_type = '.';
    return clauses;
}

void add_bookmark_event(ISpTTSEngineSite* site, ULONGLONG audio_offset_bytes, const wchar_t* bookmark)
{
    if (!site || !bookmark) {
        return;
    }

    const auto len = wcslen(bookmark);
    const auto sizeBytes = static_cast<ULONG>((len + 1) * sizeof(wchar_t));
    auto* pMem = static_cast<wchar_t*>(::CoTaskMemAlloc(sizeBytes));
    if (!pMem) {
        return;
    }
    memcpy(pMem, bookmark, sizeBytes);

    SPEVENT ev;
    memset(&ev, 0, sizeof(ev));
    ev.eEventId = SPEI_TTS_BOOKMARK;
    ev.elParamType = SPET_LPARAM_IS_STRING;
    ev.ullAudioStreamOffset = audio_offset_bytes;
    ev.lParam = reinterpret_cast<LPARAM>(pMem);
    ev.wParam = static_cast<WPARAM>(_wtol(pMem));  // Numeric bookmark ID for NVDA

    DEBUG_LOG("TGSpeechSapi: bookmark id=%ld str='%ls' at byte %llu",
              static_cast<long>(ev.wParam), pMem, audio_offset_bytes);

    site->AddEvents(&ev, 1);
}

void add_sentence_boundary_event(ISpTTSEngineSite* site, ULONGLONG audio_offset_bytes, ULONG text_offset)
{
    if (!site) {
        return;
    }

    SPEVENT ev;
    memset(&ev, 0, sizeof(ev));
    ev.eEventId = SPEI_SENTENCE_BOUNDARY;
    ev.elParamType = SPET_LPARAM_IS_UNDEFINED;
    ev.ullAudioStreamOffset = audio_offset_bytes;
    ev.lParam = static_cast<LPARAM>(text_offset);

    site->AddEvents(&ev, 1);
}

} // namespace

// ── Process-global runtime cache ──
// SAPI creates/destroys a COM instance per registered voice during
// enumeration.  Each runtime does a full eSpeak + pack init (~1 sec).
// With 26 voices that's 26 seconds of startup lag.
// Fix: when a COM instance is destroyed, cache its runtime instead of
// tearing it down.  The next COM instance grabs it — zero init, just
// a language switch.
static std::unique_ptr<tgsb::runtime> g_cached_runtime;
static std::mutex g_cache_mutex;

ISpTTSEngineImpl::ISpTTSEngineImpl()
    : sample_buf_(2048)
{
    // Try to reuse a cached runtime from a previous COM instance.
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    if (g_cached_runtime) {
        rt_ = std::move(g_cached_runtime);
        // Leave the synthesizer empty (purge() drains what it queues), so
        // nothing stale fills the Win7 audio buffer and causes Write(0) on
        // the next Speak().
        rt_->purge();
    } else {
        rt_ = std::make_unique<tgsb::runtime>();
    }
}

ISpTTSEngineImpl::~ISpTTSEngineImpl()
{
    // Return the runtime to the cache instead of destroying it.
    // If there's already a cached runtime, let ours be destroyed normally.
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    if (!g_cached_runtime && rt_) {
        rt_->purge();
        g_cached_runtime = std::move(rt_);
    }
}

STDMETHODIMP ISpTTSEngineImpl::SetObjectToken(ISpObjectToken* pToken)
{
    if (!pToken) {
        return E_INVALIDARG;
    }

    // SetObjectToken can be called while a host is speaking/previewing.
    // Keep it fast and thread-safe: just capture token + attributes.
    std::lock_guard<std::mutex> lock(token_mutex_);

    token_ = pToken;

    // Read custom attributes from the token.
    lang_tag_.clear();
    preset_name_.clear();

    try {
        ISpDataKeyPtr attrs;
        if (FAILED(pToken->OpenKey(L"Attributes", &attrs)) || !attrs) {
            // Not fatal: fall back to defaults so we remain usable even if a
            // host passes a token without our custom attributes.
            lang_tag_ = L"en-us";
            preset_name_ = L"Adam";
            return S_OK;
        }

        utils::out_ptr<wchar_t> val(CoTaskMemFree);
        if (SUCCEEDED(attrs->GetStringValue(L"TGSpeech_LangTag", val.address())) && val.get()) {
            lang_tag_ = val.get();
        }

        val.reset();
        if (SUCCEEDED(attrs->GetStringValue(L"TGSpeech_Preset", val.address())) && val.get()) {
            preset_name_ = val.get();
        }

        if (lang_tag_.empty()) {
            lang_tag_ = L"en-us";
        }
        if (preset_name_.empty()) {
            preset_name_ = L"Adam";
        }

        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP ISpTTSEngineImpl::GetObjectToken(ISpObjectToken** ppToken)
{
    if (!ppToken) {
        return E_POINTER;
    }

    std::lock_guard<std::mutex> lock(token_mutex_);

    if (!token_) {
        *ppToken = nullptr;
        return SPERR_UNINITIALIZED;
    }

    token_.AddRef();
    *ppToken = token_.GetInterfacePtr();
    return S_OK;
}

STDMETHODIMP ISpTTSEngineImpl::GetOutputFormat(const GUID* /*pTargetFmtId*/,
                                               const WAVEFORMATEX* /*pTargetWaveFormatEx*/,
                                               GUID* pOutputFormatId,
                                               WAVEFORMATEX** ppCoMemOutputWaveFormatEx)
{
    if (!pOutputFormatId || !ppCoMemOutputWaveFormatEx) {
        return E_POINTER;
    }

    *pOutputFormatId = SPDFID_WaveFormatEx;

    auto* fmt = static_cast<WAVEFORMATEX*>(::CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    if (!fmt) {
        return E_OUTOFMEMORY;
    }

    fmt->wFormatTag = WAVE_FORMAT_PCM;
    fmt->nChannels = k_audio_channels;
    // Use the runtime's actual sample rate (which reads from settings.ini).
    // If the runtime hasn't been initialized yet, ensure_initialized reads
    // the sample rate from settings before creating speechPlayer.
    if (rt_) {
        (void)rt_->ensure_initialized();
        fmt->nSamplesPerSec = static_cast<DWORD>(rt_->sample_rate());
    } else {
        fmt->nSamplesPerSec = k_default_audio_sample_rate;
    }
    fmt->wBitsPerSample = k_audio_bits_per_sample;
    fmt->nBlockAlign = (fmt->nChannels * fmt->wBitsPerSample) / 8;
    fmt->nAvgBytesPerSec = fmt->nSamplesPerSec * fmt->nBlockAlign;
    fmt->cbSize = 0;

    *ppCoMemOutputWaveFormatEx = fmt;
    return S_OK;
}

STDMETHODIMP ISpTTSEngineImpl::Speak(DWORD /*dwSpeakFlags*/,
                                     REFGUID /*rguidFormatId*/,
                                     const WAVEFORMATEX* /*pWaveFormatEx*/,
                                     const SPVTEXTFRAG* pTextFragList,
                                     ISpTTSEngineSite* pOutputSite)
{
    if (!pTextFragList || !pOutputSite) {
        return E_INVALIDARG;
    }

    // Serialize Speak() calls. Some hosts may call Speak concurrently
    // (e.g., voice preview + queued UI speech).
    std::lock_guard<std::mutex> speakLock(speak_mutex_);

    // Snapshot token-derived settings (thread-safe).
    std::wstring lang_tag;
    std::wstring preset_name;
    {
        std::lock_guard<std::mutex> lock(token_mutex_);
        lang_tag = lang_tag_;
        preset_name = preset_name_;
    }

    if (!rt_) {
        try {
            rt_ = std::make_unique<tgsb::runtime>();
        }
        catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        }
    }

    // Initialize runtime.
    HRESULT hr = rt_->ensure_initialized();
    if (FAILED(hr)) {
        DEBUG_LOG("TGSpeechSapi: runtime initialization failed 0x%08X", hr);
        return hr;
    }

    // Read the settings before anything is logged, so that the logging
    // switch takes hold with the first line of this call (#211).
    (void)tgsb::get_settings_cached(rt_->base_dir());

    // Apply language.
    if (!lang_tag.empty()) {
        (void)rt_->set_language(lang_tag);
    }

    // Each Speak() call is an utterance of its own: nothing about how the
    // last one ended carries into this one (#127).
    rt_->begin_stream();

    speak_context ctx;
    ctx.site = pOutputSite;

	// Some SAPI hosts communicate "global" rate/volume via the engine site
	// rather than populating SPVTEXTFRAG::State on every fragment. Others do
	// the opposite. Grab the site values once and treat fragment values as
	// overrides.
	long siteRateAdj = 0;
	USHORT siteVolume = 100;
	if (FAILED(pOutputSite->GetRate(&siteRateAdj))) {
		siteRateAdj = 0;
	}
	siteRateAdj = std::clamp(siteRateAdj, -10L, 10L);
	if (FAILED(pOutputSite->GetVolume(&siteVolume))) {
		siteVolume = 100;
	}
	if (siteVolume > 100) {
		siteVolume = 100;
	}

    // Audio buffer (samples -> bytes).
    // Reuse the same buffer across Speak() calls to avoid per-call heap churn.
    auto& sampleBuf = sample_buf_;
    if (sampleBuf.size() != 2048) {
        sampleBuf.resize(2048);
    }


    // ── Phase 1: Collect fragments into batches ──
    // JAWS inserts a bookmark between every word, creating separate SPVA_Speak
    // fragments.  Synthesizing each word independently loses cross-word
    // coarticulation and inserts audible gaps.  Fix: batch consecutive Speak
    // fragments, synthesize once, fire bookmarks at proportional byte offsets.

    struct pending_bookmark {
        std::wstring mark;
        size_t char_offset;
        ULONGLONG byte_threshold;
    };

    struct speak_batch {
        std::wstring text;
        std::vector<pending_bookmark> bookmarks;
        SPVSTATE first_state;
        ULONG first_text_src_offset;
        bool has_state;
        bool spell;  // read character by character (SPVA_SpellOut)
    };

    std::vector<speak_batch> batches;
    {
        speak_batch cur{};
        cur.has_state = false;

        const SPVTEXTFRAG* f = pTextFragList;
        int frag_idx = 0;
        while (f) {
            // Log every fragment JAWS sends us.
            if (f->pTextStart && f->ulTextLen > 0) {
                std::string narrow(f->ulTextLen + 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, f->pTextStart, (int)f->ulTextLen,
                                    &narrow[0], (int)narrow.size(), nullptr, nullptr);
                DEBUG_LOG("FRAG[%d] action=%d len=%lu srcOff=%lu text='%s'",
                          frag_idx, (int)f->State.eAction, f->ulTextLen,
                          f->ulTextSrcOffset, narrow.c_str());
            } else {
                DEBUG_LOG("FRAG[%d] action=%d len=%lu srcOff=%lu text=(null)",
                          frag_idx, (int)f->State.eAction, f->ulTextLen,
                          f->ulTextSrcOffset);
            }
            ++frag_idx;

            switch (f->State.eAction) {
            case SPVA_Bookmark: {
                pending_bookmark bm{};
                if (f->pTextStart && f->ulTextLen > 0)
                    bm.mark.assign(f->pTextStart, f->ulTextLen);
                else if (f->pTextStart)
                    bm.mark = f->pTextStart;
                bm.char_offset = cur.text.size();
                bm.byte_threshold = 0;
                cur.bookmarks.push_back(std::move(bm));
                break;
            }
            case SPVA_Speak: {
                if (!cur.has_state) {
                    cur.first_state = f->State;
                    cur.first_text_src_offset = f->ulTextSrcOffset;
                    cur.has_state = true;
                }
                if (f->pTextStart && f->ulTextLen > 0) {
                    // JAWS sends each word as a separate fragment without
                    // whitespace.  Ensure words don't run together.
                    if (!cur.text.empty()) {
                        wchar_t last = cur.text.back();
                        wchar_t first = f->pTextStart[0];
                        bool added_space = false;
                        if (last != L' ' && last != L'\t' && last != L'\n' &&
                            first != L' ' && first != L'\t' && first != L'\n') {
                            cur.text += L' ';
                            added_space = true;
                        }
                        DEBUG_LOG("  SPEAK: last=0x%04X first=0x%04X space=%s",
                                  (unsigned)last, (unsigned)first,
                                  added_space ? "YES" : "NO");
                    }
                    cur.text.append(f->pTextStart, f->ulTextLen);
                }
                break;
            }
            case SPVA_SpellOut: {
                // SpellOut must not merge with Speak — flush accumulated text.
                if (!cur.text.empty() || !cur.bookmarks.empty()) {
                    batches.push_back(std::move(cur));
                    cur = {};
                    cur.has_state = false;
                }
                // The spelled text is a batch of its own: SAPI hands over
                // everything inside <spell> as one fragment and leaves the
                // spelling to the engine (#207).
                speak_batch sb{};
                sb.first_state = f->State;
                sb.first_text_src_offset = f->ulTextSrcOffset;
                sb.has_state = true;
                sb.spell = true;
                if (f->pTextStart && f->ulTextLen > 0)
                    sb.text.assign(f->pTextStart, f->ulTextLen);
                batches.push_back(std::move(sb));
                break;
            }
            default:
                if (!cur.text.empty() || !cur.bookmarks.empty()) {
                    batches.push_back(std::move(cur));
                    cur = {};
                    cur.has_state = false;
                }
                break;
            }
            f = f->pNext;
        }
        if (!cur.text.empty() || !cur.bookmarks.empty())
            batches.push_back(std::move(cur));
    }

    // Log final batched text for each batch.
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        auto& b = batches[bi];
        std::string narrow(b.text.size() * 3 + 1, '\0');
        int n = WideCharToMultiByte(CP_UTF8, 0, b.text.c_str(), (int)b.text.size(),
                                    &narrow[0], (int)narrow.size(), nullptr, nullptr);
        if (n > 0) narrow.resize(n);
        DEBUG_LOG("BATCH[%zu] chars=%zu bookmarks=%zu text='%s'",
                  bi, b.text.size(), b.bookmarks.size(), narrow.c_str());
    }

    // Every batch's clauses (for spelled text, its characters), and where the
    // sentences start: at the first clause of a batch and after a clause that
    // ends with a full stop, a question mark or an exclamation mark.
    const int pm = tgsb::get_settings_cached(rt_->base_dir()).pauseMode;
    struct text_pos { size_t batch; size_t clause; };
    std::vector<std::vector<sapi_clause>> batch_clauses(batches.size());
    std::vector<text_pos> sentences;
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& b = batches[bi];
        if (b.text.empty()) continue;
        batch_clauses[bi] = b.spell ? spell_clauses(b.text) : split_clauses(b.text, pm);
        for (size_t ci = 0; ci < batch_clauses[bi].size(); ++ci) {
            const char before = ci ? batch_clauses[bi][ci - 1].clause_type : '.';
            if (before == '.' || before == '?' || before == '!')
                sentences.push_back({ bi, ci });
        }
    }

    // A skip request (ISpVoice::Skip, a reading app's "next sentence").  The
    // host says how many sentences, forward or back, or 0 for the start of
    // the current one.  The engine skips as many as the text has and reports
    // that number; if it is the number asked for, speech goes on from there,
    // otherwise the call ends (SAPI 5.4, TTS Engine Vendor Porting Guide,
    // "Getting Real-Time Action Requests").  Every skip used to be answered
    // with 0 and the end of the call (#208).  Returns false when the call
    // ends, and otherwise leaves the place to go on from in skip_to.
    text_pos skip_to{};
    auto take_skip = [&](size_t at_batch, size_t at_clause) -> bool {
        SPVSKIPTYPE type = SPVST_SENTENCE;
        long wanted = 0;
        long done = 0;
        size_t cur = 0;  // the sentence being spoken: the last one that starts at or before here
        const bool known = SUCCEEDED(pOutputSite->GetSkipInfo(&type, &wanted)) &&
                           type == SPVST_SENTENCE && !sentences.empty();
        if (known) {
            for (size_t i = 0; i < sentences.size(); ++i) {
                if (sentences[i].batch < at_batch ||
                    (sentences[i].batch == at_batch && sentences[i].clause <= at_clause))
                    cur = i;
            }
            const long back = static_cast<long>(cur);
            const long ahead = static_cast<long>(sentences.size() - 1 - cur);
            done = std::clamp(wanted, -back, ahead);
        }
        rt_->purge();
        pOutputSite->CompleteSkip(done);
        if (!known || done != wanted) {
            ctx.aborted = true;
            return false;
        }
        rt_->begin_stream();  // the sentence starts like the first one of a call
        skip_to = sentences[static_cast<size_t>(static_cast<long>(cur) + done)];
        return true;
    };

    // ── Phase 2: Synthesize and play each batch ──
    size_t next_batch = 0;
    size_t first_clause = 0;  // the clause to begin that batch at: not 0 after a skip into it
    for (size_t bi = 0; bi < batches.size(); bi = next_batch) {
        auto& batch = batches[bi];
        const size_t start_clause = first_clause;
        next_batch = bi + 1;
        first_clause = 0;
        if (ctx.aborted) break;

        DWORD actions = pOutputSite->GetActions();
        if (actions & SPVES_ABORT) {
            rt_->purge();
            ctx.aborted = true;
            break;
        }
        if (actions & SPVES_SKIP) {
            if (!take_skip(bi, start_clause)) break;
            next_batch = skip_to.batch;
            first_clause = skip_to.clause;
            continue;
        }

        // Bookmarks-only batch — fire at current audio position.
        if (batch.text.empty()) {
            for (auto& bm : batch.bookmarks)
                add_bookmark_event(pOutputSite, ctx.bytes_written, bm.mark.c_str());
            continue;
        }

        // Build speak params from first fragment's state.
        tgsb::speak_params params;
        params.preset_name = preset_name.empty() ? L"Adam" : preset_name;
        // The host's volume and rate (SetVolume, SetRate) combine with the
        // fragment's (<volume>, <rate>), as SAPI asks of an engine ("the
        // engine should combine these values with values obtained through
        // ISpTTSEngineSite::GetRate calls to arrive at a final value", TTS
        // Engine Vendor Porting Guide; likewise GetVolume).  A fragment
        // without markup carries 100 and 0, so the fragment's value cannot
        // replace the host's: at host volume 50 "level 100" came out
        // quieter than "level 80", and at host rate +5 "speed 2" slower
        // than no markup (#206).  The volume is a share of the host's, the
        // rate adds to it.
        params.volume = std::clamp(static_cast<double>(siteVolume) / 100.0, 0.0, 1.0);
        if (batch.has_state)
            params.volume *= std::clamp(static_cast<double>(batch.first_state.Volume) / 100.0, 0.0, 1.0);
        params.user_index_base = static_cast<int>(batch.first_text_src_offset);

        long rateAdj = siteRateAdj + (batch.has_state ? std::clamp(batch.first_state.RateAdj, -10L, 10L) : 0L);
        rateAdj = std::clamp(rateAdj, -10L, 10L);
        params.speed = std::clamp(std::pow(2.0, static_cast<double>(rateAdj) / 5.0), 0.25, 4.0);

        // Rate controls from settings: override system rate and/or rate boost.
        double time_stretch = 1.0;  // the DSP's output clock vs the frontend's durations
        {
            const auto& stg = tgsb::get_settings_cached(rt_->base_dir());
            if (stg.overrideSystemRate) {
                // Slider 0-100 maps to 0.3-4.0x
                double t = std::clamp(stg.globalRate, 0, 100) / 100.0;
                params.speed = 0.3 + t * (4.0 - 0.3);
            }
            // The frontend is asked for at most 2x; the rest of the speed is
            // the DSP's time stretch, which plays the 2x speech faster
            // without making any sound shorter.  Every other host splits
            // the speed this way (the NVDA driver's _SYNTH_CAP).  Handed
            // 4x, the frontend ran into its minimum durations: SAPI rate
            // +10 was 2.7 times as fast as rate 0, not 4, and mushier (#203).
            // Rate boost doubles the speed first, as in the NVDA driver, so
            // only what is then above 2x is time stretch.  It was a fixed
            // time stretch of 35% here, a much smaller boost than the same
            // setting gives on the other hosts (#209).
            if (stg.rateBoostEnabled) {
                params.speed *= k_rate_boost;
            }
            if (params.speed > k_synth_speed_cap) {
                time_stretch = params.speed / k_synth_speed_cap;
                params.speed = k_synth_speed_cap;
            }
            rt_->set_time_stretch(time_stretch);
        }

        const double pitch_slider = std::clamp(
            50.0 + 5.0 * static_cast<double>(batch.has_state ? batch.first_state.PitchAdj.MiddleAdj : 0),
            0.0, 100.0);
        params.base_pitch = 25.0 + 21.25 * (pitch_slider / 12.5);
        params.inflection = std::clamp(
            k_default_inflection * std::pow(2.0,
                static_cast<double>(batch.has_state ? batch.first_state.PitchAdj.RangeAdj : 0) / 10.0),
            0.0, 1.0);

        add_sentence_boundary_event(pOutputSite, ctx.bytes_written, batch.first_text_src_offset);


        // Stream as it renders (#128).  The batch used to be synthesized
        // into one buffer first and written afterwards, so the first byte
        // reached the host only when the whole text had been rendered and
        // the time to the first sound grew with the length of a line
        // (Narrator on long lines).  Now each clause is queued on its own and
        // every block the synthesizer produces goes to the site at once, so
        // an unpunctuated line starts as fast as a short one and an abort
        // lands within one block.  Bookmarks fire at a position proportional
        // to their character offset inside the clause they fall in (before:
        // proportional across the whole batch).
        // Clauses for intonation and the pauses after them; for spelled
        // text, its characters.
        const auto& clauses = batch_clauses[bi];
        const size_t total_chars = batch.text.size();
        std::vector<tgsb::sample_t> audio_buf;
        size_t bm_idx = 0;
        // After a skip into this batch, the marks inside the text that was
        // skipped over are not reported.
        if (start_clause > 0 && start_clause <= clauses.size()) {
            const size_t skipped_end = clauses[start_clause - 1].start + clauses[start_clause - 1].len;
            while (bm_idx < batch.bookmarks.size() && batch.bookmarks[bm_idx].char_offset < skipped_end)
                ++bm_idx;
        }
        bool skipped = false;  // a skip was taken inside this batch: go on at skip_to

        auto fire_bookmark = [&](size_t idx) {
            add_bookmark_event(pOutputSite, ctx.bytes_written,
                               batch.bookmarks[idx].mark.c_str());
        };

        for (size_t ci = start_clause; ci < clauses.size() && !ctx.aborted; ++ci) {
            const auto& clause = clauses[ci];
            const bool last_clause = (ci + 1 == clauses.size());
            const size_t clause_end = last_clause ? total_chars : clauses[ci + 1].start;

            std::wstring clauseText = padEmojiWithSpacesW(
                batch.text.substr(clause.start, clause.len));
            params.clause_type = clause.clause_type;
            params.whole_text = batch.spell || clauses.size() == 1;
            hr = rt_->queue_text(clauseText, params);
            if (FAILED(hr)) {
                DEBUG_LOG("TGSpeechSapi: queue_text failed 0x%08X", hr);
                continue;
            }
            // Byte thresholds for the bookmarks that fall inside this clause,
            // proportional to their character position within it.  Nothing
            // has been rendered yet, so the clause length is an estimate: the
            // frames the frontend queued, brought into the DSP's output clock
            // (above 2x the DSP plays them faster, so the output is shorter
            // than the queued minimum).  Marks that miss fire at the clause end.
            const ULONGLONG clause_bytes =
                static_cast<ULONGLONG>(static_cast<double>(rt_->queued_samples()) / time_stretch) * sizeof(tgsb::sample_t);
            const ULONGLONG clause_start_bytes = ctx.bytes_written;
            const size_t span = (clause_end > clause.start) ? (clause_end - clause.start) : 1;
            for (size_t k = bm_idx; k < batch.bookmarks.size(); ++k) {
                auto& bm = batch.bookmarks[k];
                if (!last_clause && bm.char_offset >= clause_end) break;
                const size_t off = (bm.char_offset > clause.start) ? (bm.char_offset - clause.start) : 0;
                bm.byte_threshold = clause_start_bytes +
                    ((off >= span) ? clause_bytes : (static_cast<ULONGLONG>(off) * clause_bytes) / span);
                bm.byte_threshold -= bm.byte_threshold % sizeof(tgsb::sample_t);  // sample-aligned
            }
            auto bookmark_in_clause = [&](size_t idx) {
                return last_clause || batch.bookmarks[idx].char_offset < clause_end;
            };

            // An abort from the host ends the call; a skip ends this clause,
            // and the call too when it cannot be met.
            auto host_stopped = [&]() -> bool {
                const DWORD a = pOutputSite->GetActions();
                if (a & SPVES_ABORT) {
                    rt_->purge();
                    ctx.aborted = true;
                    return true;
                }
                if (a & SPVES_SKIP) {
                    skipped = take_skip(bi, ci);
                    return true;
                }
                return false;
            };

            // Hand samples to the site, firing the clause's bookmarks as
            // their positions are passed.  A full host buffer (Write took
            // nothing) is waited out while polling for an abort.
            auto hand_over = [&](const tgsb::sample_t* samples, size_t count) -> bool {
                const BYTE* data = reinterpret_cast<const BYTE*>(samples);
                const size_t total = count * sizeof(tgsb::sample_t);
                size_t pos = 0;  // bytes the host has taken so far
                while (pos < total) {
                    if (host_stopped()) return false;
                    while (bm_idx < batch.bookmarks.size() && bookmark_in_clause(bm_idx) &&
                           ctx.bytes_written >= batch.bookmarks[bm_idx].byte_threshold) {
                        fire_bookmark(bm_idx);
                        ++bm_idx;
                    }
                    size_t chunk = std::min(total - pos, sampleBuf.size() * sizeof(tgsb::sample_t));
                    // Cut the block at the next bookmark, so the mark is
                    // queued at its own offset before the audio it belongs to
                    // rather than at the end of a 2048-sample block.
                    if (bm_idx < batch.bookmarks.size() && bookmark_in_clause(bm_idx)) {
                        const ULONGLONG th = batch.bookmarks[bm_idx].byte_threshold;
                        if (th > ctx.bytes_written && th - ctx.bytes_written < chunk)
                            chunk = static_cast<size_t>(th - ctx.bytes_written);
                    }
                    const ULONGLONG before = ctx.bytes_written;
                    if (!write_bytes(pOutputSite, data + pos, static_cast<ULONG>(chunk), ctx.bytes_written)) {
                        ctx.aborted = true;
                        return false;
                    }
                    // Advance by what the host took: Write may accept part of
                    // a chunk and then nothing (full buffer), and the rest is
                    // retried after a short wait.
                    const size_t took = static_cast<size_t>(ctx.bytes_written - before);
                    if (took == 0) {
                        Sleep(5);
                        continue;
                    }
                    pos += took;
                }
                return true;
            };

            // Render and write in one loop: each block goes out as soon as
            // the synthesizer produces it.
            bool clause_wrote = false;
            bool stopped = false;
            for (;;) {
                if (host_stopped()) { stopped = true; break; }
                const int got = rt_->synthesize(static_cast<int>(sampleBuf.size()), sampleBuf.data());
                if (got <= 0) break;
                clause_wrote = true;
                if (!hand_over(sampleBuf.data(), static_cast<size_t>(got))) { stopped = true; break; }
            }
            if (stopped) break;

            // The pause after the clause, as every host leaves it.  A clause
            // that made no sound (a dash rule, an ellipsis) leaves none, but
            // its bookmarks fire like any other clause's (#204).
            if (clause_wrote && clause.pause_ms > 0.0) {
                auto padSamples = static_cast<size_t>(clause.pause_ms * rt_->sample_rate() / 1000.0 + 0.5);
                audio_buf.assign(padSamples, tgsb::sample_t{0});
                if (!hand_over(audio_buf.data(), audio_buf.size())) break;
            }
            // Whatever belonged to this clause and has not fired yet (a
            // bookmark at its very end) fires now, before the next clause.
            while (bm_idx < batch.bookmarks.size() && bookmark_in_clause(bm_idx)) {
                fire_bookmark(bm_idx);
                ++bm_idx;
            }
        }

        if (ctx.aborted) break;  // marks after an abort are not reported
        if (skipped) {           // nor the ones a skip passed over
            next_batch = skip_to.batch;
            first_clause = skip_to.clause;
            continue;
        }

        // Fire any remaining bookmarks (end-of-batch), also when the batch
        // made no sound: a host that waits for a mark must get it (#204).
        while (bm_idx < batch.bookmarks.size()) {
            fire_bookmark(bm_idx);
            ++bm_idx;
        }
    }

    // Pad trailing silence so SAPI hosts that cut playback on Speak()
    // return don't clip the final syllable.  Some hosts (e.g. Balabolka)
    // stop audio immediately when Speak() returns, before the sound card
    // finishes draining its buffer.  50ms of silence gives enough runway.
    if (!ctx.aborted) {
        const int padSamples = (rt_ ? rt_->sample_rate() : k_default_audio_sample_rate) / 20;  // 50ms
        std::vector<tgsb::sample_t> silence(static_cast<size_t>(padSamples));
        std::memset(silence.data(), 0, silence.size() * sizeof(tgsb::sample_t));
        const ULONG padBytes = static_cast<ULONG>(padSamples * sizeof(tgsb::sample_t));
        write_bytes(pOutputSite, reinterpret_cast<const BYTE*>(silence.data()),
                    padBytes, ctx.bytes_written);
    }

    return S_OK;
}

} // namespace sapi
} // namespace TGSpeech
