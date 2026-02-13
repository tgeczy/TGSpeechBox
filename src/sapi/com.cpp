/*
TGSpeechBox — COM object factory and class registration framework.
Based on BSTSpeech SAPI5 Wrapper (github.com/gozaltech/BstSpeech-sapi).
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include <algorithm>
#include <stdexcept>
#include "com.hpp"

namespace TGSpeech {
namespace com {

wchar_t* strdup(const std::wstring& s)
{
    const std::size_t size = s.size();
    auto* b = static_cast<wchar_t*>(CoTaskMemAlloc((size + 1) * sizeof(wchar_t)));
    if (!b) {
        throw std::bad_alloc();
    }
    std::copy(s.begin(), s.end(), b);
    b[size] = L'\0';
    return b;
}

std::atomic<long> object_counter::count_{0};

HRESULT class_object_factory::create(REFCLSID rclsid, REFIID riid, void** ppv) const noexcept
{
    if (!ppv) {
        return E_POINTER;
    }
    *ppv = nullptr;

    for (const auto& creator : creators_) {
        if (creator->matches(rclsid)) {
            try {
                return creator->create(riid, ppv);
            }
            catch (const std::bad_alloc&) {
                return E_OUTOFMEMORY;
            }
            catch (...) {
                return E_UNEXPECTED;
            }
        }
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

class_registrar::class_registrar(HINSTANCE dll_handle)
{
    wchar_t buffer[MAX_PATH + 1];
    const DWORD size = GetModuleFileNameW(dll_handle, buffer, MAX_PATH);
    if (size == 0) {
        throw std::runtime_error("Unable to get the path of the dll");
    }
    buffer[size] = L'\0';
    dll_path_.assign(buffer);
}

const std::wstring class_registrar::clsid_key_path(L"Software\\Classes\\CLSID");
}
}
