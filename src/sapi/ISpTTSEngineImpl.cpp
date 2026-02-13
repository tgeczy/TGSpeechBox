/*
TGSpeechBox — SAPI ISpTTSEngineSite TTS engine implementation.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "ISpTTSEngineImpl.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <string>
#include <vector>

#include <sperror.h>

#include "utils.hpp"
#include "debug_log.h"

namespace TGSpeech {
namespace sapi {

namespace {

constexpr WORD k_audio_channels = 1;
constexpr DWORD k_audio_sample_rate = 16000;
constexpr WORD k_audio_bits_per_sample = 16;

constexpr double k_default_inflection = 0.55;

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

char detect_clause_type(const std::wstring& text)
{
    // Find last non-space character.
    for (auto it = text.rbegin(); it != text.rend(); ++it) {
        const wchar_t c = *it;
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
            continue;
        }
        if (c == L'.') return '.';
        if (c == L',') return ',';
        if (c == L'?') return '?';
        if (c == L'!') return '!';
        break;
    }
    return '.';
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

ISpTTSEngineImpl::ISpTTSEngineImpl()
    : rt_(std::make_unique<tgsb::runtime>())
    , sample_buf_(2048)
{
}

ISpTTSEngineImpl::~ISpTTSEngineImpl() = default;

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
    fmt->nSamplesPerSec = k_audio_sample_rate;
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

    // Apply language.
    if (!lang_tag.empty()) {
        (void)rt_->set_language(lang_tag);
    }

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

    const SPVTEXTFRAG* frag = pTextFragList;
    while (frag) {
        // Handle SAPI actions.
        switch (frag->State.eAction) {
        case SPVA_Bookmark:
            if (frag->pTextStart) {
                add_bookmark_event(pOutputSite, ctx.bytes_written, frag->pTextStart);
            }
            frag = frag->pNext;
            continue;
        case SPVA_Speak:
        case SPVA_SpellOut:
            break;
        default:
            frag = frag->pNext;
            continue;
        }

        // Check abort/skip.
        DWORD actions = pOutputSite->GetActions();
        if (actions & SPVES_ABORT) {
            rt_->purge();
            break;
        }
        if (actions & SPVES_SKIP) {
            pOutputSite->CompleteSkip(0);
            rt_->purge();
            break;
        }

        // Extract fragment text.
        // SAPI *usually* provides pTextStart for SPVA_Speak/SPVA_SpellOut, but
        // be defensive: a null pointer here will AV and take down the host
        // process (NVDA, SpeechUX, etc.).
        std::wstring text;
        if (frag->pTextStart && frag->ulTextLen > 0) {
            text.assign(frag->pTextStart, frag->ulTextLen);
        }
        if (!text.empty()) {
            // Emit a sentence boundary at the start of this fragment.
            add_sentence_boundary_event(pOutputSite, ctx.bytes_written, frag->ulTextSrcOffset);

            tgsb::speak_params params;
            params.preset_name = preset_name.empty() ? L"Adam" : preset_name;
            // Volume:
            // - Prefer global volume from the engine site (matches how many SAPI
            //   hosts communicate volume).
            // - Allow per-fragment overrides to take precedence when present.
            params.volume = std::clamp(static_cast<double>(siteVolume) / 100.0, 0.0, 1.0);
            if (frag->State.Volume != 100) {
                params.volume = std::clamp(static_cast<double>(frag->State.Volume) / 100.0, 0.0, 1.0);
            }
            params.user_index_base = static_cast<int>(frag->ulTextSrcOffset);

            // RateAdj in SAPI is typically -10..10.
            // Some hosts provide the "global" rate via ISpTTSEngineSite::GetRate,
            // leaving per-fragment RateAdj at 0. Treat RateAdj as an override and
            // fall back to the site value when needed.
            long rateAdj = frag->State.RateAdj;
            if (rateAdj == 0) {
                rateAdj = siteRateAdj;
            }
            rateAdj = std::clamp(rateAdj, -10L, 10L);
            params.speed = std::pow(2.0, static_cast<double>(rateAdj) / 5.0);
            params.speed = std::clamp(params.speed, 0.25, 4.0);

            // Map pitch: SAPI MiddleAdj (-10..10) -> NVDA pitch slider (0..100, default 50).
            const double pitch_slider = std::clamp(50.0 + 5.0 * static_cast<double>(frag->State.PitchAdj.MiddleAdj), 0.0, 100.0);
            params.base_pitch = 25.0 + 21.25 * (pitch_slider / 12.5);

            // Map inflection: keep NVDA-like default, adjust by RangeAdj.
            params.inflection = k_default_inflection * std::pow(2.0, static_cast<double>(frag->State.PitchAdj.RangeAdj) / 10.0);
            params.inflection = std::clamp(params.inflection, 0.0, 1.0);

            params.clause_type = detect_clause_type(text);

            hr = rt_->queue_text(text, params);
            if (FAILED(hr)) {
                DEBUG_LOG("TGSpeechSapi: queue_text failed 0x%08X", hr);
                // Continue with next fragment rather than hard failing; SAPI may prefer partial output.
            }

            // Drain queued audio.
            for (;;) {
                actions = pOutputSite->GetActions();
                if (actions & SPVES_ABORT) {
                    rt_->purge();
                    ctx.aborted = true;
                    break;
                }
                if (actions & SPVES_SKIP) {
                    pOutputSite->CompleteSkip(0);
                    rt_->purge();
                    ctx.aborted = true;
                    break;
                }

                const int got = rt_->synthesize(static_cast<int>(sampleBuf.size()), sampleBuf.data());
                if (got <= 0) {
                    break;
                }

                const ULONG bytes = static_cast<ULONG>(got * sizeof(tgsb::sample_t));
                const BYTE* data = reinterpret_cast<const BYTE*>(sampleBuf.data());

                if (!write_bytes(pOutputSite, data, bytes, ctx.bytes_written)) {
                    ctx.aborted = true;
                    break;
                }
            }
        }

        if (ctx.aborted) {
            break;
        }

        frag = frag->pNext;
    }

    return S_OK;
}

} // namespace sapi
} // namespace TGSpeech
