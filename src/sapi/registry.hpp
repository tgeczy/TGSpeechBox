/*
TGSpeechBox — Windows registry helper declarations.
Based on BSTSpeech SAPI5 Wrapper (github.com/gozaltech/BstSpeech-sapi).
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

#include <windows.h>
#include <string>
#include <stdexcept>

namespace TGSpeech {
namespace registry {

class error : public std::runtime_error
{
public:
    explicit error(const std::string& msg) : std::runtime_error(msg) {}
};

class key
{
public:
    key(HKEY parent, const std::wstring& name, REGSAM access_mask = KEY_READ, bool create = false)
    {
        const LONG result = create
            ? RegCreateKeyExW(parent, name.c_str(), 0, nullptr, 0, access_mask, nullptr, &handle_, nullptr)
            : RegOpenKeyExW(parent, name.c_str(), 0, access_mask, &handle_);

        if (result != ERROR_SUCCESS) {
            throw error("Unable to open/create a registry key");
        }
    }

    ~key()
    {
        if (handle_) {
            RegCloseKey(handle_);
        }
    }

    key(const key&) = delete;
    key& operator=(const key&) = delete;

    key(key&& other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    key& operator=(key&& other) noexcept
    {
        if (this != &other) {
            if (handle_) {
                RegCloseKey(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] operator HKEY() const noexcept
    {
        return handle_;
    }

    void delete_subkey(const std::wstring& name)
    {
        if (RegDeleteKeyW(handle_, name.c_str()) != ERROR_SUCCESS) {
            throw error("Unable to delete a registry key");
        }
    }

    [[nodiscard]] std::wstring get(const std::wstring& name) const;

    [[nodiscard]] std::wstring get() const
    {
        return get(L"");
    }

    void set(const std::wstring& name, const std::wstring& value);

    void set(const std::wstring& value)
    {
        set(L"", value);
    }

private:
    HKEY handle_ = nullptr;
};

}
}
