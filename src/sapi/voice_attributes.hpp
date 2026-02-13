/*
TGSpeechBox — Voice definition structure and attributes.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

#include <string>
#include <vector>

namespace TGSpeech {
namespace sapi {

enum class voice_preset {
    Adam,
    Benjamin,
    Caleb,
    David,
};

struct voice_preset_info {
    voice_preset preset;
    const wchar_t* display_name;
    const wchar_t* gender; // "Male" / "Female" (SAPI attribute string)
};

inline constexpr voice_preset_info k_presets[] = {
    {voice_preset::Adam, L"Adam", L"Male"},
    {voice_preset::Benjamin, L"Benjamin", L"Male"},
    {voice_preset::Caleb, L"Caleb", L"Male"},
    {voice_preset::David, L"David", L"Male"},
};

struct voice_def {
    std::wstring name;          // SAPI "Name" attribute
    std::wstring language_lcid; // SAPI "Language" attribute (hex string, e.g. "409")
    std::wstring gender;        // SAPI "Gender" attribute
    std::wstring vendor;        // SAPI "Vendor" attribute

    // Custom attributes (stored in token->Attributes) so the engine can configure runtime.
    std::wstring lang_tag;      // e.g. "en-us"
    std::wstring preset_name;   // e.g. "Adam"
};

class voice_attributes {
public:
    explicit voice_attributes(voice_def def) : def_(std::move(def)) {}

    [[nodiscard]] std::wstring get_name() const { return def_.name; }
    [[nodiscard]] std::wstring get_age() const { return L"Adult"; }
    [[nodiscard]] std::wstring get_gender() const { return def_.gender; }
    [[nodiscard]] std::wstring get_language() const { return def_.language_lcid; }
    [[nodiscard]] std::wstring get_vendor() const { return def_.vendor; }

    // Custom attributes
    [[nodiscard]] std::wstring get_lang_tag() const { return def_.lang_tag; }
    [[nodiscard]] std::wstring get_preset_name() const { return def_.preset_name; }

private:
    voice_def def_;
};

} // namespace sapi
} // namespace TGSpeech
