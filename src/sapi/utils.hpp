/*
TGSpeechBox — String conversion and COM pointer utilities.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

#include <cstring>
#include <string>
#include <string_view>
#include <memory>
#include <windows.h>

namespace TGSpeech {
namespace utils {

[[nodiscard]] inline std::wstring string_to_wstring(const std::string& s)
{
    if (s.empty()) {
        return {};
    }
    const int size_needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                                 static_cast<int>(s.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(size_needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        result.data(), size_needed);
    return result;
}

[[nodiscard]] inline std::wstring string_to_wstring(const char* s)
{
    if (!s || !*s) {
        return {};
    }
    const int len = static_cast<int>(std::strlen(s));
    const int size_needed = MultiByteToWideChar(CP_UTF8, 0, s, len, nullptr, 0);
    std::wstring result(static_cast<size_t>(size_needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, len, result.data(), size_needed);
    return result;
}

[[nodiscard]] inline std::string wstring_to_string(const std::wstring& s)
{
    if (s.empty()) {
        return {};
    }
    const int size_needed = WideCharToMultiByte(CP_UTF8, 0, s.c_str(),
                                                 static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size_needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        result.data(), size_needed, nullptr, nullptr);
    return result;
}

[[nodiscard]] inline std::string wstring_to_string(const wchar_t* s)
{
    if (!s || !*s) {
        return {};
    }
    const int len = static_cast<int>(wcslen(s));
    const int size_needed = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size_needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, len, result.data(), size_needed, nullptr, nullptr);
    return result;
}

[[nodiscard]] inline std::string wstring_to_string(const wchar_t* s, std::size_t n)
{
    if (!s || n == 0) {
        return {};
    }
    const int size_needed = WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(n),
                                                 nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size_needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(n),
                        result.data(), size_needed, nullptr, nullptr);
    return result;
}

template<typename T>
class out_ptr
{
public:
    template<typename F>
    explicit out_ptr(F f)
        : ptr_(nullptr)
        , deleter_(std::make_unique<deleter_impl<F>>(f))
    {
    }

    ~out_ptr()
    {
        release();
    }

    out_ptr(const out_ptr&) = delete;
    out_ptr& operator=(const out_ptr&) = delete;

    [[nodiscard]] T* get() const noexcept
    {
        return ptr_;
    }

    T** address() noexcept
    {
        release();
        return &ptr_;
    }

    void reset() noexcept
    {
        release();
    }

private:
    class deleter_base
    {
    public:
        virtual ~deleter_base() = default;
        virtual void destroy(T* p) const noexcept = 0;
    };

    template<typename F>
    class deleter_impl : public deleter_base
    {
    public:
        explicit deleter_impl(F f) : func_(f) {}

        void destroy(T* p) const noexcept override
        {
            func_(p);
        }

    private:
        F func_;
    };

    void release() noexcept
    {
        if (ptr_) {
            deleter_->destroy(ptr_);
            ptr_ = nullptr;
        }
    }

    T* ptr_;
    std::unique_ptr<deleter_base> deleter_;
};

}
}
