/*
TGSpeechBox — Voice profiles as the phoneme editor holds them, and "Save to Profile".
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

// A voice profile's data, and how the speech settings' voicing sliders turn
// into a profile's voice source and back.  No Win32 here, so tests/cpp can
// check it on every platform.

#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

namespace tgsb_editor {


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

// ---------------------------------------------------------------------------
// Voice source (voicingTone) of a profile, as the editor's sliders see it.
// The voicing sliders are the keys the frontend reads from a profile's
// voicingTone block, in slider order: all 19 of them, the two chorus keys
// included (a profile may carry chorus since 3.10, #124; the list stopped
// at 17 until #219).
constexpr int kProfileToneKeyCount = 19;
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

// A profile's voice source as slider positions: each key the profile has,
// or what the engine plays without it, at the closest slider position.
// outValues, if given, gets the exact values in slider order.
void profileToneToSliders(const VPVoiceProfile& profile, std::vector<int>& voicingSliders,
                          std::vector<double>* outValues);

// The voicing sliders as a save request's tone values.  sourceSliders are
// the positions the sliders had when sourceProfile was loaded; a slider
// still there has not moved.  Without them every slider counts as moved.
void slidersToSaveRequest(const std::vector<int>& voicingSliders, const std::string& sourceProfile,
                          const std::vector<int>& sourceSliders, ProfileSaveRequest& req);

} // namespace tgsb_editor
