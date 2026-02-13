/*
TGSpeechBox — SAPI ISpDataKey implementation for voice token attributes.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include <iterator>
#include "ISpDataKeyImpl.hpp"

namespace TGSpeech {
namespace sapi {

STDMETHODIMP ISpDataKeyImpl::GetData(LPCWSTR /*pszValueName*/, ULONG* /*pcbData*/, BYTE* /*pData*/)
{
    return SPERR_NOT_FOUND;
}

STDMETHODIMP ISpDataKeyImpl::GetStringValue(LPCWSTR pszValueName, LPWSTR* ppszValue)
{
    if (!ppszValue) {
        return E_POINTER;
    }
    *ppszValue = nullptr;

    try {
        if (!pszValueName || pszValueName[0] == L'\0') {
            *ppszValue = com::strdup(default_value_);
        } else {
            auto it = values_.find(pszValueName);
            if (it == values_.end()) {
                return SPERR_NOT_FOUND;
            }
            *ppszValue = com::strdup(it->second);
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

STDMETHODIMP ISpDataKeyImpl::GetDWORD(LPCWSTR /*pszKeyName*/, DWORD* /*pdwValue*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP ISpDataKeyImpl::OpenKey(LPCWSTR /*pszSubKeyName*/, ISpDataKey** /*ppSubKey*/)
{
    return SPERR_NOT_FOUND;
}

STDMETHODIMP ISpDataKeyImpl::EnumKeys(ULONG /*Index*/, LPWSTR* /*ppszSubKeyName*/)
{
    return SPERR_NO_MORE_ITEMS;
}

STDMETHODIMP ISpDataKeyImpl::EnumValues(ULONG Index, LPWSTR* ppszValueName)
{
    if (Index >= values_.size()) {
        return SPERR_NO_MORE_ITEMS;
    }
    if (!ppszValueName) {
        return E_POINTER;
    }
    *ppszValueName = nullptr;

    try {
        auto it = values_.begin();
        std::advance(it, Index);
        *ppszValueName = com::strdup(it->first);
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP ISpDataKeyImpl::SetData(LPCWSTR /*pszValueName*/, ULONG /*cbData*/, const BYTE* /*pData*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP ISpDataKeyImpl::SetStringValue(LPCWSTR /*pszValueName*/, LPCWSTR /*pszValue*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP ISpDataKeyImpl::SetDWORD(LPCWSTR /*pszValueName*/, DWORD /*dwValue*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP ISpDataKeyImpl::CreateKey(LPCWSTR /*pszSubKeyName*/, ISpDataKey** /*ppSubKey*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP ISpDataKeyImpl::DeleteKey(LPCWSTR /*pszSubKeyName*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP ISpDataKeyImpl::DeleteValue(LPCWSTR /*pszValueName*/)
{
    return E_NOTIMPL;
}
}
}
