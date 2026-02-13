/*
TGSpeechBox — SAPI voice token interface.
Based on BSTSpeech SAPI5 Wrapper (github.com/gozaltech/BstSpeech-sapi).
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

#include <map>
#include <comdef.h>
#include <comip.h>

#include "utils.hpp"
#include "voice_attributes.hpp"
#include "ISpDataKeyImpl.hpp"

namespace TGSpeech {
namespace sapi {

class voice_token : public ISpDataKeyImpl
{
public:
    explicit voice_token(const voice_attributes& attr);

    STDMETHOD(OpenKey)(LPCWSTR pszSubKeyName, ISpDataKey** ppSubKey) override;
    STDMETHOD(EnumKeys)(ULONG Index, LPWSTR* ppszSubKeyName) override;

private:
    [[nodiscard]] bool str_equal(const std::wstring& s1, const std::wstring& s2) const noexcept
    {
        return _wcsicmp(s1.c_str(), s2.c_str()) == 0;
    }

    using attribute_map = std::map<std::wstring, std::wstring, str_less>;

    attribute_map attributes_;
};
}
}
