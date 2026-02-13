/*
TGSpeechBox — SAPI ISpDataKey interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

#include <string>
#include <map>
#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>

#include "com.hpp"

namespace TGSpeech {
namespace sapi {

class ISpDataKeyImpl : public ISpDataKey
{
public:
    STDMETHOD(GetData)(LPCWSTR pszValueName, ULONG* pcbData, BYTE* pData) override;
    STDMETHOD(GetStringValue)(LPCWSTR pszValueName, LPWSTR* ppszValue) override;
    STDMETHOD(GetDWORD)(LPCWSTR pszKeyName, DWORD* pdwValue) override;
    STDMETHOD(OpenKey)(LPCWSTR pszSubKeyName, ISpDataKey** ppSubKey) override;
    STDMETHOD(EnumKeys)(ULONG Index, LPWSTR* ppszSubKeyName) override;
    STDMETHOD(EnumValues)(ULONG Index, LPWSTR* ppszValueName) override;
    STDMETHOD(SetData)(LPCWSTR pszValueName, ULONG cbData, const BYTE* pData) override;
    STDMETHOD(SetStringValue)(LPCWSTR pszValueName, LPCWSTR pszValue) override;
    STDMETHOD(SetDWORD)(LPCWSTR pszValueName, DWORD dwValue) override;
    STDMETHOD(CreateKey)(LPCWSTR pszSubKeyName, ISpDataKey** ppSubKey) override;
    STDMETHOD(DeleteKey)(LPCWSTR pszSubKeyName) override;
    STDMETHOD(DeleteValue)(LPCWSTR pszValueName) override;

    void set(const std::wstring& name, const std::wstring& value)
    {
        values_[name] = value;
    }

    void set(const std::wstring& value)
    {
        default_value_ = value;
    }

protected:
    struct str_less
    {
        [[nodiscard]] bool operator()(const std::wstring& s1, const std::wstring& s2) const noexcept
        {
            return _wcsicmp(s1.c_str(), s2.c_str()) < 0;
        }
    };

    [[nodiscard]] void* get_interface(REFIID riid) noexcept
    {
        return com::try_primary_interface<ISpDataKey>(this, riid);
    }

private:
    using value_map = std::map<std::wstring, std::wstring, str_less>;

    std::wstring default_value_;
    value_map values_;
};
}
}
