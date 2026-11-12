/*
TGSpeechBox — Voice profile editor interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <array>

#include "profile_tone.h"

namespace tgsb_editor {

// Forward declaration
class TgsbRuntime;

// Dialog state for voice profile list
struct VoiceProfilesDialogState {
  std::vector<VPVoiceProfile> profiles;
  std::wstring phonemesYamlPath;
  bool modified = false;
  bool ok = false;
};

// Dialog state for editing a single profile
struct EditVoiceProfileDialogState {
  VPVoiceProfile profile;
  std::string currentClass;  // Currently selected class in combo
  bool ok = false;
};

// Dialog state for editing phoneme override
struct EditPhonemeOverrideDialogState {
  VPPhonemeOverride override;
  bool ok = false;
};

// Available class names for the combo box
const char* const kVoiceProfileClasses[] = {
  "vowel",
  "consonant",
  "voicedConsonant",
  "voicedFricative",
  "unvoicedFricative",
  "nasal",
  "liquid",
  "semivowel",
  "stop",
  "affricate"
};
constexpr int kVoiceProfileClassCount = sizeof(kVoiceProfileClasses) / sizeof(kVoiceProfileClasses[0]);

// Available multiplier field names
const char* const kScaleFieldNames[] = {
  "voicePitch_mul",
  "endVoicePitch_mul",
  "vibratoPitchOffset_mul",
  "vibratoSpeed_mul",
  "voiceTurbulenceAmplitude_mul",
  "glottalOpenQuotient_mul",
  "voiceAmplitude_mul",
  "aspirationAmplitude_mul",
  "fricationAmplitude_mul",
  "preFormantGain_mul",
  "outputGain_mul",
  "cf_mul",
  "pf_mul",
  "cb_mul",
  "pb_mul",
  "pa_mul"
};
constexpr int kScaleFieldCount = sizeof(kScaleFieldNames) / sizeof(kScaleFieldNames[0]);

// Available phoneme override field names (absolute values, not multipliers)
const char* const kOverrideFieldNames[] = {
  "cf1", "cf2", "cf3", "cf4", "cf5", "cf6",
  "pf1", "pf2", "pf3", "pf4", "pf5", "pf6",
  "cb1", "cb2", "cb3", "cb4", "cb5", "cb6",
  "pb1", "pb2", "pb3", "pb4", "pb5", "pb6",
  "pa1", "pa2", "pa3", "pa4", "pa5", "pa6",
  "voicePitch", "endVoicePitch",
  "voiceAmplitude", "aspirationAmplitude", "fricationAmplitude",
  "voiceTurbulenceAmplitude", "glottalOpenQuotient",
  "vibratoPitchOffset", "vibratoSpeed",
  "preFormantGain", "outputGain", "parallelBypass"
};
constexpr int kOverrideFieldCount = sizeof(kOverrideFieldNames) / sizeof(kOverrideFieldNames[0]);

// Load voice profiles from phonemes.yaml
bool loadVoiceProfilesFromYaml(const std::wstring& yamlPath, std::vector<VPVoiceProfile>& outProfiles, std::string& outError);

// Save voice profiles back to phonemes.yaml (preserves other content)
bool saveVoiceProfilesToYaml(const std::wstring& yamlPath, const std::vector<VPVoiceProfile>& profiles, std::string& outError);

// Dialog entry points
bool ShowVoiceProfilesDialog(HINSTANCE hInst, HWND parent, VoiceProfilesDialogState& st);
bool ShowEditVoiceProfileDialog(HINSTANCE hInst, HWND parent, EditVoiceProfileDialogState& st);
bool ShowEditPhonemeOverrideDialog(HINSTANCE hInst, HWND parent, EditPhonemeOverrideDialogState& st);

} // namespace tgsb_editor
