/*
Voice Profile System for NV Speech Player Frontend

Implementation of voice profile loading and application.

Copyright 2014-2024 NV Access Limited.
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

// Parse a single VoiceProfile from a YAML map node.
static bool parseVoiceProfile(const std::string& name, const yaml_min::Node& node, VoiceProfile& out, std::string& outError) {
  if (!node.isMap()) {
    outError = "Voice profile '" + name + "' must be a map";
    return false;
  }
  
  out.name = name;
  
  // Parse classScales.
  const yaml_min::Node* classScalesNode = node.get("classScales");
  if (classScalesNode && classScalesNode->isMap()) {
    for (const auto& kv : classScalesNode->map) {
      const std::string& className = kv.first;
      ClassScales scales;
      if (!parseClassScales(kv.second, scales, outError)) {
        return false;
      }
      out.classScales[className] = scales;
    }
  }
  
  // Parse phonemeOverrides.
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
