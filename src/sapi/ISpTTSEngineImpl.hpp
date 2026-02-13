/*
TGSpeechBox — SAPI TTS engine interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <comdef.h>
#include <comip.h>

#include "com.hpp"
#include "tgsb_runtime.hpp"

namespace TGSpeech {
namespace sapi {

// CLSID for the engine COM class (in-proc server).
// NOTE: This must stay in sync with registration in sapi_main.cpp and voice_token.
class __declspec(uuid("{70E56986-4B3C-4CE1-B1F1-C861EE906FFD}")) ISpTTSEngineImpl :
    public ISpTTSEngine,
    public ISpObjectWithToken
{
public:
    ISpTTSEngineImpl();
    ~ISpTTSEngineImpl();

    ISpTTSEngineImpl(const ISpTTSEngineImpl&) = delete;
    ISpTTSEngineImpl& operator=(const ISpTTSEngineImpl&) = delete;

    STDMETHOD(Speak)(DWORD dwSpeakFlags,
                     REFGUID rguidFormatId,
                     const WAVEFORMATEX* pWaveFormatEx,
                     const SPVTEXTFRAG* pTextFragList,
                     ISpTTSEngineSite* pOutputSite) override;

    STDMETHOD(GetOutputFormat)(const GUID* pTargetFmtId,
                               const WAVEFORMATEX* pTargetWaveFormatEx,
                               GUID* pOutputFormatId,
                               WAVEFORMATEX** ppCoMemOutputWaveFormatEx) override;

    STDMETHOD(SetObjectToken)(ISpObjectToken* pToken) override;
    STDMETHOD(GetObjectToken)(ISpObjectToken** ppToken) override;

protected:
    [[nodiscard]] void* get_interface(REFIID riid) noexcept
    {
        void* ptr = com::try_primary_interface<ISpTTSEngine>(this, riid);
        return ptr ? ptr : com::try_interface<ISpObjectWithToken>(this, riid);
    }

private:
    _COM_SMARTPTR_TYPEDEF(ISpObjectToken, __uuidof(ISpObjectToken));
    _COM_SMARTPTR_TYPEDEF(ISpDataKey, __uuidof(ISpDataKey));

    ISpObjectTokenPtr token_;

    // Custom attributes stored in the token.
    std::wstring lang_tag_;
    std::wstring preset_name_;

    // Protects token_ and the configuration strings.
    mutable std::mutex token_mutex_;

    // Serialize Speak() calls and guard runtime usage.
    mutable std::mutex speak_mutex_;

    std::unique_ptr<tgsb::runtime> rt_;

    // Reusable audio buffer for the Speak() loop.
    // Speak() can be called very frequently, so avoid allocating/freeing on
    // every call.
    std::vector<tgsb::sample_t> sample_buf_;
};

} // namespace sapi
} // namespace TGSpeech
