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

namespace tgsb_editor {

// Forward declaration
class TgsbRuntime;

// Multiplier field types for class scales
struct VPClassScales {
  // Generic map for storing all scale fields
  // This allows flexible read/write of any scale field without hardcoding
  std::map<std::string, double> scales;
  
  // Formant frequency multipliers (cf1-cf6, pf1-pf6)
  std::array<double, 6> cf_mul = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
  std::array<double, 6> pf_mul = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
  
  // Formant bandwidth multipliers (cb1-cb6, pb1-pb6)
  std::array<double, 6> cb_mul = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
  std::array<double, 6> pb_mul = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
  
  // Parallel amplitude multipliers (pa1-pa6)
  std::array<double, 6> pa_mul = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
  
  // Scalar multipliers (with _set flags to track if explicitly set)
  double voicePitch_mul = 1.0;           bool voicePitch_mul_set = false;
  double endVoicePitch_mul = 1.0;        bool endVoicePitch_mul_set = false;
  double vibratoPitchOffset_mul = 1.0;   bool vibratoPitchOffset_mul_set = false;
  double vibratoSpeed_mul = 1.0;         bool vibratoSpeed_mul_set = false;
  double voiceTurbulenceAmplitude_mul = 1.0; bool voiceTurbulenceAmplitude_mul_set = false;
  double glottalOpenQuotient_mul = 1.0;  bool glottalOpenQuotient_mul_set = false;
  double voiceAmplitude_mul = 1.0;       bool voiceAmplitude_mul_set = false;
  double aspirationAmplitude_mul = 1.0;  bool aspirationAmplitude_mul_set = false;
  double fricationAmplitude_mul = 1.0;   bool fricationAmplitude_mul_set = false;
  double preFormantGain_mul = 1.0;       bool preFormantGain_mul_set = false;
  double outputGain_mul = 1.0;           bool outputGain_mul_set = false;
  
  // Track which array elements are explicitly set (vs default)
  std::array<bool, 6> cf_mul_set = {false, false, false, false, false, false};
  std::array<bool, 6> pf_mul_set = {false, false, false, false, false, false};
  std::array<bool, 6> cb_mul_set = {false, false, false, false, false, false};
  std::array<bool, 6> pb_mul_set = {false, false, false, false, false, false};
  std::array<bool, 6> pa_mul_set = {false, false, false, false, false, false};
};

// Phoneme override: map field name -> absolute value
struct VPPhonemeOverride {
  std::string phoneme;
  std::map<std::string, double> fields;  // field name -> value
};

// A complete voice profile
struct VPVoiceProfile {
  std::string name;
  std::map<std::string, VPClassScales> classScales;  // class name -> scales
  std::vector<VPPhonemeOverride> phonemeOverrides;

  // Optional voicingTone section from phonemes.yaml.
  // The editor UI doesn't expose these yet, but we parse + write them back so
  // users don't lose manual edits.
  bool hasVoicingTone = false;
  std::map<std::string, std::string> voicingTone;

  // Multiplier on the listener's inflection (profile-level `inflectionScale`).
  double inflectionScale = 1.0;
  bool hasInflectionScale = false;
};

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
// ---------------------------------------------------------------------------
// Voice source (voicingTone) of a profile, as the editor's sliders see it.
// The first kProfileToneKeyCount voicing sliders are the keys the frontend
// reads from a profile's voicingTone block, in slider order.
constexpr int kProfileToneKeyCount = 17;
extern const char* const kProfileToneKeys[kProfileToneKeyCount];
// What a built-in voice plays with every slider at its neutral position.
double profileToneNeutral(int keyIndex);
// What the engine uses for a key a profile leaves out: the frontend fills a
// profile's voicingTone block from its own defaults (high shelf 5.5 dB); a
// profile without a block plays the DSP defaults.
double profileToneFallback(int keyIndex, bool profileHasToneBlock);

// A scale typed by the user: the whole string is a finite number in 0..3.
bool parseScaleStrict(const std::string& text, double& out);

// One "Save to Profile" request from the speech settings.
struct ProfileSaveRequest {
  std::string name;              // destination profile
  std::string sourceProfile;     // profile the sliders were loaded from ("" = none)
  std::string baseVoice;         // built-in voice whose shape seeds a new profile ("" = none)
  double tone[kProfileToneKeyCount] = {};    // slider values mapped to parameter values
  bool toneMoved[kProfileToneKeyCount] = {}; // slider moved since it was loaded from sourceProfile
  double inflectionScale = 1.0;  // the profile's own scale; 1 = the listener's inflection
};

// Apply a request to a loaded profile list.  A new destination starts as a
// copy of sourceProfile (class scales, overrides, voice source) or, without
// one, with baseVoice's shape.  Voice-source keys whose slider did not move
// keep their stored text; moved keys are written, and a key at the neutral
// value is left out, so an untouched save adds nothing.  outNote reports
// what a built-in voice's shape could not carry over.
void applyProfileSave(std::vector<VPVoiceProfile>& profiles, const ProfileSaveRequest& req, std::string& outNote);

bool loadVoiceProfilesFromYaml(const std::wstring& yamlPath, std::vector<VPVoiceProfile>& outProfiles, std::string& outError);

// Save voice profiles back to phonemes.yaml (preserves other content)
bool saveVoiceProfilesToYaml(const std::wstring& yamlPath, const std::vector<VPVoiceProfile>& profiles, std::string& outError);

// Dialog entry points
bool ShowVoiceProfilesDialog(HINSTANCE hInst, HWND parent, VoiceProfilesDialogState& st);
bool ShowEditVoiceProfileDialog(HINSTANCE hInst, HWND parent, EditVoiceProfileDialogState& st);
bool ShowEditPhonemeOverrideDialog(HINSTANCE hInst, HWND parent, EditPhonemeOverrideDialogState& st);

} // namespace tgsb_editor
