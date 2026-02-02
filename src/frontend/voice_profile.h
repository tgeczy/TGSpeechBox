/*
Voice Profile System for NV Speech Player Frontend

This module provides optional "voice profiles" that can transform phoneme
parameters to produce different voice qualities (e.g., female voice) without
maintaining separate phoneme tables.

Design principles:
- Zero breaking changes: packs without voice profiles work exactly as before.
- No reshaping: existing phonemes are the base; profiles are overlays.
- Class-based transforms using existing phoneme flags (_isVowel, _isVoiced, etc.)
- Per-phoneme overrides for fine-tuning.

Copyright 2014-2026 Tamas Geczy
Licensed under GNU General Public License version 2.0.
*/

#ifndef NVSP_FRONTEND_VOICE_PROFILE_H
#define NVSP_FRONTEND_VOICE_PROFILE_H

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations to avoid circular dependency with pack.h.
namespace nvsp_frontend {
namespace yaml_min { struct Node; }
struct PhonemeDef;
}

namespace nvsp_frontend {

// Frame field count - must match kFrameFieldCount in pack.h.
constexpr int kVoiceProfileFrameFieldCount = 47;

// Number of formant frequency/bandwidth fields (cf1-cf6, pf1-pf6, cb1-cb6, pb1-pb6).
constexpr int kFormantCount = 6;

// Class-based scaling factors.
// Each array has kFormantCount elements for cf1-cf6, pf1-pf6, etc.
struct ClassScales {
  // Cascade formant frequency multipliers (cf1..cf6).
  std::array<double, kFormantCount> cf_mul = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
  
  // Parallel formant frequency multipliers (pf1..pf6).
  std::array<double, kFormantCount> pf_mul = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
  
  // Cascade formant bandwidth multipliers (cb1..cb6).
  std::array<double, kFormantCount> cb_mul = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
  
  // Parallel formant bandwidth multipliers (pb1..pb6).
  std::array<double, kFormantCount> pb_mul = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
  
  // Pitch multipliers (for shifting fundamental frequency).
  double voicePitch_mul = 1.0;
  double endVoicePitch_mul = 1.0;
  bool voicePitch_mul_set = false;
  bool endVoicePitch_mul_set = false;
  
  // Vibrato multipliers.
  double vibratoPitchOffset_mul = 1.0;
  double vibratoSpeed_mul = 1.0;
  bool vibratoPitchOffset_mul_set = false;
  bool vibratoSpeed_mul_set = false;
  
  // Voice quality multipliers.
  double voiceTurbulenceAmplitude_mul = 1.0;
  double glottalOpenQuotient_mul = 1.0;
  bool voiceTurbulenceAmplitude_mul_set = false;
  bool glottalOpenQuotient_mul_set = false;
  
  // Scalar amplitude multipliers.
  double voiceAmplitude_mul = 1.0;
  double aspirationAmplitude_mul = 1.0;
  double fricationAmplitude_mul = 1.0;
  double preFormantGain_mul = 1.0;
  double outputGain_mul = 1.0;
  bool voiceAmplitude_mul_set = false;
  bool aspirationAmplitude_mul_set = false;
  bool fricationAmplitude_mul_set = false;
  bool preFormantGain_mul_set = false;
  bool outputGain_mul_set = false;
};

// Per-phoneme override values.
// These are absolute values that replace the base + class-scaled result.
struct PhonemeOverride {
  // Map from FieldId index to absolute value.
  std::unordered_map<int, double> values;
};

// Voicing tone parameters for DSP-level voice quality.
// These control the glottal pulse shape, spectral tilt, and EQ.
// All fields have sensible defaults; only non-default values need to be specified in YAML.
struct VoicingTone {
  // V1 parameters
  double voicingPeakPos = 0.0;       // Glottal pulse peak position (0.0-1.0)
  double voicedPreEmphA = 0.0;       // Pre-emphasis coefficient A
  double voicedPreEmphMix = 0.0;     // Pre-emphasis mix (0.0-1.0)
  double highShelfGainDb = 0.0;      // High shelf EQ gain in dB
  double highShelfFcHz = 0.0;        // High shelf EQ center frequency
  double highShelfQ = 0.0;           // High shelf EQ Q factor
  double voicedTiltDbPerOct = 0.0;   // Spectral tilt in dB/octave
  
  // V2 parameters
  double noiseGlottalModDepth = 0.0; // Noise modulation by glottal cycle
  double pitchSyncF1DeltaHz = 0.0;   // Pitch-synchronous F1 delta
  double pitchSyncB1DeltaHz = 0.0;   // Pitch-synchronous B1 delta
  
  // V3 parameters
  double speedQuotient = 2.0;        // Glottal speed quotient (default 2.0 = neutral)
  double aspirationTiltDbPerOct = 0.0; // Aspiration spectral tilt
  
  // Track which fields were explicitly set in YAML
  bool voicingPeakPos_set = false;
  bool voicedPreEmphA_set = false;
  bool voicedPreEmphMix_set = false;
  bool highShelfGainDb_set = false;
  bool highShelfFcHz_set = false;
  bool highShelfQ_set = false;
  bool voicedTiltDbPerOct_set = false;
  bool noiseGlottalModDepth_set = false;
  bool pitchSyncF1DeltaHz_set = false;
  bool pitchSyncB1DeltaHz_set = false;
  bool speedQuotient_set = false;
  bool aspirationTiltDbPerOct_set = false;
};

// A single voice profile definition.
struct VoiceProfile {
  std::string name;
  
  // Class-based transforms keyed by class name.
  // Supported class names:
  //   "vowel"            - _isVowel
  //   "voicedConsonant"  - !_isVowel && _isVoiced
  //   "voicedFricative"  - _isFricative && _isVoiced (fricationAmplitude > 0.05)
  //   "unvoicedFricative"- _isFricative && !_isVoiced
  //   "consonant"        - default fallback for non-vowels
  //   "nasal"            - _isNasal
  //   "liquid"           - _isLiquid
  //   "stop"             - _isStop
  //   "affricate"        - _isAfricate
  //   "semivowel"        - _isSemivowel
  std::unordered_map<std::string, ClassScales> classScales;
  
  // Per-phoneme overrides keyed by phoneme symbol (UTF-8).
  std::unordered_map<std::string, PhonemeOverride> phonemeOverrides;
  
  // DSP-level voicing tone parameters (optional).
  VoicingTone voicingTone;
  bool hasVoicingTone = false;
};

// Collection of voice profiles from a pack.
struct VoiceProfileSet {
  // Map from profile name to profile definition.
  std::unordered_map<std::string, VoiceProfile> profiles;
  
  // Check if a profile exists.
  bool hasProfile(const std::string& name) const {
    return profiles.find(name) != profiles.end();
  }
  
  // Get a profile by name, or nullptr if not found.
  const VoiceProfile* getProfile(const std::string& name) const {
    auto it = profiles.find(name);
    return (it != profiles.end()) ? &it->second : nullptr;
  }
};

// Parse voice profiles from a YAML node.
// The node should be the value of the "voiceProfiles:" key.
// Returns true on success.
bool parseVoiceProfiles(const yaml_min::Node& node, VoiceProfileSet& out, std::string& outError);

// Apply a voice profile to a token's field values.
// This is the main hook called from ipa_engine.cpp after building the base frame.
//
// Parameters:
//   field        - Array of kFrameFieldCount doubles (token field values, modified in-place).
//   setMask      - Reference to the set mask (updated when override values are applied).
//   phonemeDef   - The phoneme definition for this token (used for class detection).
//   profileSet   - The set of voice profiles from the pack (may be nullptr).
//   profileName  - The name of the voice profile to apply (empty = no-op).
//
// The function does nothing if:
//   - profileSet is nullptr
//   - profileName is empty
//   - profileName is not found in profileSet
void applyVoiceProfileToFields(
  double* field,
  std::uint64_t& setMask,
  const PhonemeDef* phonemeDef,
  const VoiceProfileSet* profileSet,
  const std::string& profileName
);

// Determine which class keys apply to a phoneme based on its flags.
// Returns a list of class keys in priority order (most general first).
// The caller should apply class scales in order, letting later classes
// compound on earlier ones.
std::vector<std::string> getPhonemeClassKeys(const PhonemeDef* def, double fricationAmplitude);

} // namespace nvsp_frontend

#endif
