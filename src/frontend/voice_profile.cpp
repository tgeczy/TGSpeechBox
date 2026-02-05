/*
Voice Profile System for NV Speech Player Frontend

Implementation of voice profile loading and application.

Copyright 2014-2026 Tamas Geczy
Licensed under GNU General Public License version 2.0.
*/

#include "voice_profile.h"
#include "pack.h"
#include "yaml_min.h"

#include <cstring>

namespace nvsp_frontend {

// Helper to parse an array of doubles from a YAML sequence or scalar.
// Fills `out` with up to `maxCount` values. Returns the number of values parsed.
// If `replicateScalar` is true and a single scalar is provided, it will be
// replicated across all `maxCount` elements.
static int parseDoubleArray(const yaml_min::Node& node, double* out, int maxCount, bool replicateScalar = false) {
  if (node.isSeq()) {
    int count = 0;
    for (const auto& item : node.seq) {
      if (count >= maxCount) break;
      double v;
      if (item.asNumber(v)) {
        out[count++] = v;
      }
    }
    return count;
  } else if (node.isScalar()) {
    double v;
    if (node.asNumber(v) && maxCount > 0) {
      if (replicateScalar) {
        // Replicate the scalar across all elements.
        // This makes "cf_mul: 1.12" scale all formants by 1.12, not just F1.
        for (int i = 0; i < maxCount; ++i) {
          out[i] = v;
        }
        return maxCount;
      } else {
        out[0] = v;
        return 1;
      }
    }
  }
  return 0;
}

// Parse ClassScales from a YAML map node.
static bool parseClassScales(const yaml_min::Node& node, ClassScales& out, std::string& /*outError*/) {
  if (!node.isMap()) return true; // Empty is fine.
  
  // Helper to parse a formant multiplier array.
  // Use replicateScalar=true so "cf_mul: 1.12" scales ALL formants by 1.12,
  // not just F1 (which would be very surprising behavior).
  auto parseMulArray = [&](const char* key, std::array<double, kFormantCount>& arr) {
    const yaml_min::Node* n = node.get(key);
    if (!n) return;
    double vals[kFormantCount];
    int count = parseDoubleArray(*n, vals, kFormantCount, /*replicateScalar=*/true);
    for (int i = 0; i < count && i < kFormantCount; ++i) {
      arr[static_cast<size_t>(i)] = vals[i];
    }
    // If fewer values provided than kFormantCount (but more than 1),
    // leave the rest at default (1.0).
  };
  
  parseMulArray("cf_mul", out.cf_mul);
  parseMulArray("pf_mul", out.pf_mul);
  parseMulArray("cb_mul", out.cb_mul);
  parseMulArray("pb_mul", out.pb_mul);
  
  // Parse scalar multipliers.
  auto parseScalarMul = [&](const char* key, double& val, bool& setFlag) {
    const yaml_min::Node* n = node.get(key);
    if (!n) return;
    double v;
    if (n->asNumber(v)) {
      val = v;
      setFlag = true;
    }
  };
  
  parseScalarMul("voiceAmplitude_mul", out.voiceAmplitude_mul, out.voiceAmplitude_mul_set);
  parseScalarMul("aspirationAmplitude_mul", out.aspirationAmplitude_mul, out.aspirationAmplitude_mul_set);
  parseScalarMul("fricationAmplitude_mul", out.fricationAmplitude_mul, out.fricationAmplitude_mul_set);
  parseScalarMul("preFormantGain_mul", out.preFormantGain_mul, out.preFormantGain_mul_set);
  parseScalarMul("outputGain_mul", out.outputGain_mul, out.outputGain_mul_set);
  
  // Pitch multipliers.
  parseScalarMul("voicePitch_mul", out.voicePitch_mul, out.voicePitch_mul_set);
  parseScalarMul("endVoicePitch_mul", out.endVoicePitch_mul, out.endVoicePitch_mul_set);
  
  // Vibrato multipliers.
  parseScalarMul("vibratoPitchOffset_mul", out.vibratoPitchOffset_mul, out.vibratoPitchOffset_mul_set);
  parseScalarMul("vibratoSpeed_mul", out.vibratoSpeed_mul, out.vibratoSpeed_mul_set);
  
  // Voice quality multipliers.
  parseScalarMul("voiceTurbulenceAmplitude_mul", out.voiceTurbulenceAmplitude_mul, out.voiceTurbulenceAmplitude_mul_set);
  parseScalarMul("glottalOpenQuotient_mul", out.glottalOpenQuotient_mul, out.glottalOpenQuotient_mul_set);
  
  return true;
}

// Parse PhonemeOverride from a YAML map node.
static bool parsePhonemeOverride(const yaml_min::Node& node, PhonemeOverride& out, std::string& /*outError*/) {
  if (!node.isMap()) return true;
  
  for (const auto& kv : node.map) {
    const std::string& fieldName = kv.first;
    const yaml_min::Node& val = kv.second;
    
    // Skip keys that don't look like field names.
    if (fieldName.empty()) continue;
    
    FieldId id;
    if (!parseFieldId(fieldName, id)) continue;
    
    double num;
    if (!val.asNumber(num)) continue;
    
    out.values[static_cast<int>(id)] = num;
  }
  
  return true;
}

// Parse VoicingTone from a YAML map node.
static bool parseVoicingTone(const yaml_min::Node& node, VoicingTone& out, std::string& /*outError*/) {
  if (!node.isMap()) return true;
  
  // Helper to parse a voicing tone parameter
  auto parseParam = [&](const char* key, double& val, bool& setFlag) {
    const yaml_min::Node* n = node.get(key);
    if (!n) return;
    double v;
    if (n->asNumber(v)) {
      val = v;
      setFlag = true;
    }
  };
  
  // V1 parameters
  parseParam("voicingPeakPos", out.voicingPeakPos, out.voicingPeakPos_set);
  parseParam("voicedPreEmphA", out.voicedPreEmphA, out.voicedPreEmphA_set);
  parseParam("voicedPreEmphMix", out.voicedPreEmphMix, out.voicedPreEmphMix_set);
  parseParam("highShelfGainDb", out.highShelfGainDb, out.highShelfGainDb_set);
  parseParam("highShelfFcHz", out.highShelfFcHz, out.highShelfFcHz_set);
  parseParam("highShelfQ", out.highShelfQ, out.highShelfQ_set);
  parseParam("voicedTiltDbPerOct", out.voicedTiltDbPerOct, out.voicedTiltDbPerOct_set);
  
  // V2 parameters
  parseParam("noiseGlottalModDepth", out.noiseGlottalModDepth, out.noiseGlottalModDepth_set);
  parseParam("pitchSyncF1DeltaHz", out.pitchSyncF1DeltaHz, out.pitchSyncF1DeltaHz_set);
  parseParam("pitchSyncB1DeltaHz", out.pitchSyncB1DeltaHz, out.pitchSyncB1DeltaHz_set);
  
  // V3 parameters
  parseParam("speedQuotient", out.speedQuotient, out.speedQuotient_set);
  parseParam("aspirationTiltDbPerOct", out.aspirationTiltDbPerOct, out.aspirationTiltDbPerOct_set);
  parseParam("cascadeBwScale", out.cascadeBwScale, out.cascadeBwScale_set);
  parseParam("tremorDepth", out.tremorDepth, out.tremorDepth_set);
  
  return true;
}

// Parse a single VoiceProfile from a YAML map node.
// Supports both nested and dotted-key formats for classScales:
//   Nested:  classScales: { vowel: { cf_mul: [...] } }
//   Dotted:  classScales: { vowel.cf_mul: [...], vowel.voicePitch_mul: 1.4 }
// Both can be mixed in the same profile.
static bool parseVoiceProfile(const std::string& name, const yaml_min::Node& node, VoiceProfile& out, std::string& outError) {
  if (!node.isMap()) {
    outError = "Voice profile '" + name + "' must be a map";
    return false;
  }
  
  out.name = name;
  
  // Parse classScales with dotted-key expansion.
  const yaml_min::Node* classScalesNode = node.get("classScales");
  if (classScalesNode && classScalesNode->isMap()) {
    // First pass: collect all class names (from both nested and dotted keys).
    // For dotted keys like "vowel.cf_mul", extract "vowel" as the class name.
    // For nested keys like "vowel", use as-is.
    
    for (const auto& kv : classScalesNode->map) {
      const std::string& key = kv.first;
      
      // Check if this is a dotted key (e.g., "vowel.cf_mul")
      size_t dotPos = key.find('.');
      if (dotPos != std::string::npos) {
        // Dotted key: "className.fieldName"
        std::string className = key.substr(0, dotPos);
        std::string fieldName = key.substr(dotPos + 1);
        
        // Ensure the class exists in our map
        if (out.classScales.find(className) == out.classScales.end()) {
          out.classScales[className] = ClassScales{};
        }
        ClassScales& scales = out.classScales[className];
        
        // Parse the field value into the appropriate ClassScales member.
        // This is a bit repetitive but keeps it simple and explicit.
        const yaml_min::Node& val = kv.second;
        
        // Formant array multipliers
        auto tryParseMulArray = [&](const char* fname, std::array<double, kFormantCount>& arr) -> bool {
          if (fieldName != fname) return false;
          double vals[kFormantCount];
          int count = parseDoubleArray(val, vals, kFormantCount, /*replicateScalar=*/true);
          for (int i = 0; i < count && i < kFormantCount; ++i) {
            arr[static_cast<size_t>(i)] = vals[i];
          }
          return true;
        };
        
        if (tryParseMulArray("cf_mul", scales.cf_mul)) continue;
        if (tryParseMulArray("pf_mul", scales.pf_mul)) continue;
        if (tryParseMulArray("cb_mul", scales.cb_mul)) continue;
        if (tryParseMulArray("pb_mul", scales.pb_mul)) continue;
        
        // Scalar multipliers
        auto tryParseScalarMul = [&](const char* fname, double& v, bool& flag) -> bool {
          if (fieldName != fname) return false;
          double num;
          if (val.asNumber(num)) {
            v = num;
            flag = true;
          }
          return true;
        };
        
        if (tryParseScalarMul("voiceAmplitude_mul", scales.voiceAmplitude_mul, scales.voiceAmplitude_mul_set)) continue;
        if (tryParseScalarMul("aspirationAmplitude_mul", scales.aspirationAmplitude_mul, scales.aspirationAmplitude_mul_set)) continue;
        if (tryParseScalarMul("fricationAmplitude_mul", scales.fricationAmplitude_mul, scales.fricationAmplitude_mul_set)) continue;
        if (tryParseScalarMul("preFormantGain_mul", scales.preFormantGain_mul, scales.preFormantGain_mul_set)) continue;
        if (tryParseScalarMul("outputGain_mul", scales.outputGain_mul, scales.outputGain_mul_set)) continue;
        if (tryParseScalarMul("voicePitch_mul", scales.voicePitch_mul, scales.voicePitch_mul_set)) continue;
        if (tryParseScalarMul("endVoicePitch_mul", scales.endVoicePitch_mul, scales.endVoicePitch_mul_set)) continue;
        if (tryParseScalarMul("vibratoPitchOffset_mul", scales.vibratoPitchOffset_mul, scales.vibratoPitchOffset_mul_set)) continue;
        if (tryParseScalarMul("vibratoSpeed_mul", scales.vibratoSpeed_mul, scales.vibratoSpeed_mul_set)) continue;
        if (tryParseScalarMul("voiceTurbulenceAmplitude_mul", scales.voiceTurbulenceAmplitude_mul, scales.voiceTurbulenceAmplitude_mul_set)) continue;
        if (tryParseScalarMul("glottalOpenQuotient_mul", scales.glottalOpenQuotient_mul, scales.glottalOpenQuotient_mul_set)) continue;
        
        // Unknown dotted field - ignore silently for forward compatibility
      } else {
        // Nested key: "className" with a map value
        if (!kv.second.isMap()) {
          // Could be an error, but let's be lenient
          continue;
        }
        const std::string& className = key;
        ClassScales scales;
        // If we already have some values from dotted keys, start with those
        auto existingIt = out.classScales.find(className);
        if (existingIt != out.classScales.end()) {
          scales = existingIt->second;
        }
        if (!parseClassScales(kv.second, scales, outError)) {
          return false;
        }
        out.classScales[className] = scales;
      }
    }
  }
  
  // Parse phonemeOverrides (no dotted-key expansion needed here, it's already flat).
  const yaml_min::Node* overridesNode = node.get("phonemeOverrides");
  if (overridesNode && overridesNode->isMap()) {
    for (const auto& kv : overridesNode->map) {
      const std::string& phonemeKey = kv.first;
      PhonemeOverride ovr;
      if (!parsePhonemeOverride(kv.second, ovr, outError)) {
        return false;
      }
      if (!ovr.values.empty()) {
        out.phonemeOverrides[phonemeKey] = ovr;
      }
    }
  }
  
  // Parse voicingTone (DSP-level voice quality parameters).
  const yaml_min::Node* voicingToneNode = node.get("voicingTone");
  if (voicingToneNode && voicingToneNode->isMap()) {
    if (!parseVoicingTone(*voicingToneNode, out.voicingTone, outError)) {
      return false;
    }
    // Mark that this profile has explicit voicing tone settings
    out.hasVoicingTone = true;
  }
  
  return true;
}

bool parseVoiceProfiles(const yaml_min::Node& node, VoiceProfileSet& out, std::string& outError) {
  out.profiles.clear();
  
  if (!node.isMap()) {
    // Not an error; just no profiles defined.
    return true;
  }
  
  for (const auto& kv : node.map) {
    const std::string& profileName = kv.first;
    VoiceProfile profile;
    if (!parseVoiceProfile(profileName, kv.second, profile, outError)) {
      return false;
    }
    out.profiles[profileName] = std::move(profile);
  }
  
  return true;
}

std::vector<std::string> getPhonemeClassKeys(const PhonemeDef* def, double fricationAmplitude) {
  std::vector<std::string> keys;
  
  if (!def) return keys;
  
  const std::uint32_t flags = def->flags;
  const bool isVowel = (flags & kIsVowel) != 0;
  const bool isVoiced = (flags & kIsVoiced) != 0;
  const bool isNasal = (flags & kIsNasal) != 0;
  const bool isLiquid = (flags & kIsLiquid) != 0;
  const bool isStop = (flags & kIsStop) != 0;
  const bool isAfricate = (flags & kIsAfricate) != 0;
  const bool isSemivowel = (flags & kIsSemivowel) != 0;
  
  // Determine if this is a fricative-like sound.
  // Mirrors ipa_engine.cpp: fricationAmplitude > 0.05.
  const bool isFricativeLike = (fricationAmplitude > 0.05);
  
  // Add keys from most general to most specific.
  // The caller should apply them in order, so later (more specific) classes
  // can override earlier ones.
  
  if (isVowel) {
    keys.push_back("vowel");
  } else {
    // It's a consonant.
    keys.push_back("consonant");
    
    if (isNasal) {
      keys.push_back("nasal");
    }
    if (isLiquid) {
      keys.push_back("liquid");
    }
    if (isSemivowel) {
      keys.push_back("semivowel");
    }
    if (isStop) {
      keys.push_back("stop");
    }
    if (isAfricate) {
      keys.push_back("affricate");
    }
    
    // Fricative classification.
    if (isFricativeLike) {
      if (isVoiced) {
        keys.push_back("voicedFricative");
      } else {
        keys.push_back("unvoicedFricative");
      }
    }
    
    // General voiced/voiceless consonant (applied last as a broad category).
    if (isVoiced) {
      keys.push_back("voicedConsonant");
    }
  }
  
  return keys;
}

void applyVoiceProfileToFields(
  double* field,
  std::uint64_t& setMask,
  const PhonemeDef* phonemeDef,
  const VoiceProfileSet* profileSet,
  const std::string& profileName
) {
  // Early exit conditions.
  if (!profileSet) return;
  if (profileName.empty()) return;
  
  const VoiceProfile* profile = profileSet->getProfile(profileName);
  if (!profile) return;
  
  if (!phonemeDef) return;
  
  // Get the current fricationAmplitude for class detection.
  const int faIdx = static_cast<int>(FieldId::fricationAmplitude);
  double fricationAmplitude = 0.0;
  if ((setMask & (1ull << faIdx)) != 0) {
    fricationAmplitude = field[faIdx];
  }
  
  // Get the class keys for this phoneme.
  std::vector<std::string> classKeys = getPhonemeClassKeys(phonemeDef, fricationAmplitude);
  
  // Lambda to apply formant multipliers to token fields.
  auto applyFormantMul = [&](FieldId baseId, const std::array<double, kFormantCount>& muls) {
    int baseIdx = static_cast<int>(baseId);
    for (int i = 0; i < kFormantCount; ++i) {
      int idx = baseIdx + i;
      if ((setMask & (1ull << idx)) != 0) {
        field[idx] *= muls[static_cast<size_t>(i)];
      }
    }
  };
  
  // Lambda to apply a scalar multiplier if set.
  auto applyScalarMul = [&](FieldId id, double mul, bool isSet) {
    if (!isSet) return;
    int idx = static_cast<int>(id);
    if ((setMask & (1ull << idx)) != 0) {
      field[idx] *= mul;
    }
  };
  
  // Step 1: Apply class-based scales in order.
  // Each class scale is applied multiplicatively.
  for (const std::string& classKey : classKeys) {
    auto it = profile->classScales.find(classKey);
    if (it == profile->classScales.end()) continue;
    
    const ClassScales& scales = it->second;
    
    // Apply formant frequency multipliers.
    applyFormantMul(FieldId::cf1, scales.cf_mul);
    applyFormantMul(FieldId::pf1, scales.pf_mul);
    
    // Apply formant bandwidth multipliers.
    applyFormantMul(FieldId::cb1, scales.cb_mul);
    applyFormantMul(FieldId::pb1, scales.pb_mul);
    
    // Apply scalar amplitude multipliers.
    applyScalarMul(FieldId::voiceAmplitude, scales.voiceAmplitude_mul, scales.voiceAmplitude_mul_set);
    applyScalarMul(FieldId::aspirationAmplitude, scales.aspirationAmplitude_mul, scales.aspirationAmplitude_mul_set);
    applyScalarMul(FieldId::fricationAmplitude, scales.fricationAmplitude_mul, scales.fricationAmplitude_mul_set);
    applyScalarMul(FieldId::preFormantGain, scales.preFormantGain_mul, scales.preFormantGain_mul_set);
    applyScalarMul(FieldId::outputGain, scales.outputGain_mul, scales.outputGain_mul_set);
    
    // Apply pitch multipliers.
    applyScalarMul(FieldId::voicePitch, scales.voicePitch_mul, scales.voicePitch_mul_set);
    applyScalarMul(FieldId::endVoicePitch, scales.endVoicePitch_mul, scales.endVoicePitch_mul_set);
    
    // Apply vibrato multipliers.
    applyScalarMul(FieldId::vibratoPitchOffset, scales.vibratoPitchOffset_mul, scales.vibratoPitchOffset_mul_set);
    applyScalarMul(FieldId::vibratoSpeed, scales.vibratoSpeed_mul, scales.vibratoSpeed_mul_set);
    
    // Apply voice quality multipliers.
    applyScalarMul(FieldId::voiceTurbulenceAmplitude, scales.voiceTurbulenceAmplitude_mul, scales.voiceTurbulenceAmplitude_mul_set);
    applyScalarMul(FieldId::glottalOpenQuotient, scales.glottalOpenQuotient_mul, scales.glottalOpenQuotient_mul_set);
  }
  
  // Step 2: Apply per-phoneme overrides.
  // These are absolute values that replace the scaled result.
  // Use the phoneme key converted to UTF-8.
  std::string phonemeKeyUtf8 = u32ToUtf8(phonemeDef->key);
  
  auto ovrIt = profile->phonemeOverrides.find(phonemeKeyUtf8);
  if (ovrIt != profile->phonemeOverrides.end()) {
    const PhonemeOverride& ovr = ovrIt->second;
    for (const auto& kv : ovr.values) {
      int idx = kv.first;
      double val = kv.second;
      if (idx >= 0 && idx < kFrameFieldCount) {
        field[idx] = val;
        setMask |= (1ull << idx);
      }
    }
  }
}

} // namespace nvsp_frontend
