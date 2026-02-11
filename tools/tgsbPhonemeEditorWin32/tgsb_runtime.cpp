#include "tgsb_runtime.h"
#include "VoiceProfileEditor.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace tgsb_editor {

static std::string wideToUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (len <= 1) return {};
  std::string out(static_cast<size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), len, nullptr, nullptr);
  out.resize(static_cast<size_t>(len - 1));
  return out;
}

static unsigned int msToSamples(double ms, int sampleRate) {
  if (ms <= 0) return 1;
  double s = (ms / 1000.0) * static_cast<double>(sampleRate);
  if (s < 1.0) s = 1.0;
  if (s > static_cast<double>(0xFFFFFFFFu)) s = static_cast<double>(0xFFFFFFFFu);
  return static_cast<unsigned int>(s);
}

static bool tryGetBool(const Node& mapNode, const char* key, bool& out) {
  const Node* n = mapNode.get(key);
  if (!n) return false;
  bool b = false;
  if (n->asBool(b)) {
    out = b;
    return true;
  }
  return false;
}

static bool tryGetNumber(const Node& mapNode, const char* key, double& out) {
  const Node* n = mapNode.get(key);
  if (!n) return false;
  double v = 0;
  if (n->asNumber(v)) {
    out = v;
    return true;
  }
  return false;
}

struct FieldMap {
  const char* name;
  speechPlayer_frameParam_t speechPlayer_frame_t::* member;
};

static const FieldMap kFieldMap[] = {
  {"voicePitch", &speechPlayer_frame_t::voicePitch},
  {"vibratoPitchOffset", &speechPlayer_frame_t::vibratoPitchOffset},
  {"vibratoSpeed", &speechPlayer_frame_t::vibratoSpeed},
  {"voiceTurbulenceAmplitude", &speechPlayer_frame_t::voiceTurbulenceAmplitude},
  {"glottalOpenQuotient", &speechPlayer_frame_t::glottalOpenQuotient},
  {"voiceAmplitude", &speechPlayer_frame_t::voiceAmplitude},
  {"aspirationAmplitude", &speechPlayer_frame_t::aspirationAmplitude},

  {"cf1", &speechPlayer_frame_t::cf1},
  {"cf2", &speechPlayer_frame_t::cf2},
  {"cf3", &speechPlayer_frame_t::cf3},
  {"cf4", &speechPlayer_frame_t::cf4},
  {"cf5", &speechPlayer_frame_t::cf5},
  {"cf6", &speechPlayer_frame_t::cf6},
  {"cfN0", &speechPlayer_frame_t::cfN0},
  {"cfNP", &speechPlayer_frame_t::cfNP},

  {"cb1", &speechPlayer_frame_t::cb1},
  {"cb2", &speechPlayer_frame_t::cb2},
  {"cb3", &speechPlayer_frame_t::cb3},
  {"cb4", &speechPlayer_frame_t::cb4},
  {"cb5", &speechPlayer_frame_t::cb5},
  {"cb6", &speechPlayer_frame_t::cb6},
  {"cbN0", &speechPlayer_frame_t::cbN0},
  {"cbNP", &speechPlayer_frame_t::cbNP},

  {"caNP", &speechPlayer_frame_t::caNP},

  {"fricationAmplitude", &speechPlayer_frame_t::fricationAmplitude},

  {"pf1", &speechPlayer_frame_t::pf1},
  {"pf2", &speechPlayer_frame_t::pf2},
  {"pf3", &speechPlayer_frame_t::pf3},
  {"pf4", &speechPlayer_frame_t::pf4},
  {"pf5", &speechPlayer_frame_t::pf5},
  {"pf6", &speechPlayer_frame_t::pf6},

  {"pb1", &speechPlayer_frame_t::pb1},
  {"pb2", &speechPlayer_frame_t::pb2},
  {"pb3", &speechPlayer_frame_t::pb3},
  {"pb4", &speechPlayer_frame_t::pb4},
  {"pb5", &speechPlayer_frame_t::pb5},
  {"pb6", &speechPlayer_frame_t::pb6},

  {"pa1", &speechPlayer_frame_t::pa1},
  {"pa2", &speechPlayer_frame_t::pa2},
  {"pa3", &speechPlayer_frame_t::pa3},
  {"pa4", &speechPlayer_frame_t::pa4},
  {"pa5", &speechPlayer_frame_t::pa5},
  {"pa6", &speechPlayer_frame_t::pa6},

  {"parallelBypass", &speechPlayer_frame_t::parallelBypass},
  {"preFormantGain", &speechPlayer_frame_t::preFormantGain},
  {"outputGain", &speechPlayer_frame_t::outputGain},
  {"endVoicePitch", &speechPlayer_frame_t::endVoicePitch},
};

static size_t kFieldCount() {
  return sizeof(kFieldMap) / sizeof(kFieldMap[0]);
}

const std::vector<std::string>& TgsbRuntime::frameParamNames() {
  static std::vector<std::string> names;
  if (names.empty()) {
    names.reserve(kFieldCount());
    for (const auto& f : kFieldMap) names.emplace_back(f.name);
  }
  return names;
}

const std::vector<std::string>& TgsbRuntime::voicingParamNames() {
  static std::vector<std::string> names;
  if (names.empty()) {
    names = {
      "voicingPeakPos",
      "voicedPreEmphA",
      "voicedPreEmphMix",
      "highShelfGainDb",
      "highShelfFcHz",
      "highShelfQ",
      "voicedTiltDbPerOct",
      "noiseGlottalModDepth",
      "pitchSyncF1DeltaHz",
      "pitchSyncB1DeltaHz",
      "speedQuotient",
      "aspirationTiltDbPerOct",
      "cascadeBwScale",
      "tremorDepth"
    };
  }
  return names;
}

const std::vector<std::string>& TgsbRuntime::frameExParamNames() {
  static std::vector<std::string> names;
  if (names.empty()) {
    names = {
      "creakiness",
      "breathiness",
      "jitter",
      "shimmer",
      "sharpness"
    };
  }
  return names;
}

static void applyPhonemeMapToFrame(const Node& phonemeMap, speechPlayer_frame_t& frame, bool& outIsVowel) {
  outIsVowel = false;

  // Defaults that keep the preview audible.
  frame.voicePitch = 120.0;
  frame.endVoicePitch = 120.0;
  frame.preFormantGain = 1.0;
  frame.outputGain = 1.0;

  bool isVowel = false;
  if (tryGetBool(phonemeMap, "_isVowel", isVowel)) {
    outIsVowel = isVowel;
  }

  for (const auto& f : kFieldMap) {
    double v = 0.0;
    if (tryGetNumber(phonemeMap, f.name, v)) {
      frame.*(f.member) = v;
    }
  }

  // If the table doesn't provide output gain, make it a little louder for preview.
  if (frame.outputGain <= 0.0) {
    frame.outputGain = 1.2;
  }
}

TgsbRuntime::TgsbRuntime() {
  // No static layout assumptions: we convert frames field-by-field in the callback.
  m_speech.frameParams.assign(frameParamNames().size(), 50);
  m_speech.voicingParams.assign(voicingParamNames().size(), 50);
// tremorDepth (index 13) defaults to 0, not 50
if (m_speech.voicingParams.size() > 13) m_speech.voicingParams[13] = 0;
  // FrameEx params: creakiness/breathiness/jitter/shimmer default to 0, sharpness to 50
  m_speech.frameExParams.assign(frameExParamNames().size(), 0);
  if (m_speech.frameExParams.size() >= 5) m_speech.frameExParams[4] = 50; // sharpness default
}

TgsbRuntime::~TgsbRuntime() {
  unload();
}

void TgsbRuntime::setSpeechSettings(const SpeechSettings& s) {
  m_speech = s;
  if (m_speech.voiceName.empty()) m_speech.voiceName = "Adam";
  // Normalize pauseMode (matches NVDA driver: off | short | long).
  {
    std::string pm = m_speech.pauseMode;
    for (auto& ch : pm) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
    if (pm != "off" && pm != "short" && pm != "long") pm = "short";
    m_speech.pauseMode = pm;
  }
  if (m_speech.frameParams.size() != frameParamNames().size()) {
    m_speech.frameParams.assign(frameParamNames().size(), 50);
  }
  if (m_speech.voicingParams.size() != voicingParamNames().size()) {
    m_speech.voicingParams.assign(voicingParamNames().size(), 50);
    // tremorDepth (index 13) defaults to 0, not 50
    if (m_speech.voicingParams.size() > 13) m_speech.voicingParams[13] = 0;
  }
  if (m_speech.frameExParams.size() != frameExParamNames().size()) {
    m_speech.frameExParams.assign(frameExParamNames().size(), 0);
    if (m_speech.frameExParams.size() >= 5) m_speech.frameExParams[4] = 50; // sharpness default
  }
}

SpeechSettings TgsbRuntime::getSpeechSettings() const {
  return m_speech;
}

static int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static double clampDouble(double v, double lo, double hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Map slider value (0-100) to actual VoicingTone parameter value
// Each parameter has different ranges, with 50 being "neutral/default"
static double mapVoicingSliderToValue(int paramIndex, int sliderValue) {
  double sv = static_cast<double>(clampInt(sliderValue, 0, 100));
  
  switch (paramIndex) {
    case 0: // voicingPeakPos: 0.85-0.95, default 0.91 at 50
      return 0.85 + (sv / 100.0) * 0.10;
    case 1: // voicedPreEmphA: 0.0-0.97, default 0.92 at 50
      return (sv / 100.0) * 0.97;
    case 2: // voicedPreEmphMix: 0.0-1.0, default 0.35 at 50
      return sv / 100.0;
    case 3: // highShelfGainDb: -12 to +12, default 4.0 at 50
      return -12.0 + (sv / 100.0) * 24.0;
    case 4: // highShelfFcHz: 500-8000, default 2000 at 50
      return 500.0 + (sv / 100.0) * 7500.0;
    case 5: // highShelfQ: 0.3-2.0, default 0.7 at 50
      return 0.3 + (sv / 100.0) * 1.7;
    case 6: // voicedTiltDbPerOct: -24 to +24, default 0.0 at 50
      return -24.0 + (sv / 100.0) * 48.0;
    case 7: // noiseGlottalModDepth: 0.0-1.0, default 0.0 at 0
      return sv / 100.0;
    case 8: // pitchSyncF1DeltaHz: -60 to +60, default 0.0 at 50
      return -60.0 + (sv / 100.0) * 120.0;
    case 9: // pitchSyncB1DeltaHz: -50 to +50, default 0.0 at 50
      return -50.0 + (sv / 100.0) * 100.0;
    case 10: // speedQuotient: 0.5-4.0, default 2.0 at 50
      return 0.5 + (sv / 100.0) * 3.5;
    case 11: // aspirationTiltDbPerOct: -12 to +12, default 0.0 at 50
      return -12.0 + (sv / 100.0) * 24.0;
case 12: // cascadeBwScale: 0.3-2.0, default 0.9 at slider 50
      if (sv <= 50.0) {
        return 2.0 - (sv / 50.0) * 1.1;   // 0 -> 2.0, 50 -> 0.9
      }
      return 0.9 - ((sv - 50.0) / 50.0) * 0.6;  // 50 -> 0.9, 100 -> 0.3
    case 13: // tremorDepth: 0.0-0.4, default 0.0 at 0
      return (sv / 100.0) * 0.4;
    default:
      return 0.0;
  }
}

// Build VoicingTone struct with ABI header (v2+ layout, extended fields allowed)
static EditorVoicingToneV2 buildVoicingToneV2(const std::vector<int>& sliders) {
  EditorVoicingToneV2 tone{};
  
  // Set v3 header
  tone.magic = SPEECHPLAYER_VOICINGTONE_MAGIC;
  tone.structSize = sizeof(EditorVoicingToneV2);
  tone.structVersion = SPEECHPLAYER_VOICINGTONE_VERSION;
  tone.dspVersion = SPEECHPLAYER_DSP_VERSION;
  
  // Map sliders to values (use defaults if sliders vector is wrong size)
  tone.voicingPeakPos = (sliders.size() > 0) ? mapVoicingSliderToValue(0, sliders[0]) : 0.91;
  tone.voicedPreEmphA = (sliders.size() > 1) ? mapVoicingSliderToValue(1, sliders[1]) : 0.92;
  tone.voicedPreEmphMix = (sliders.size() > 2) ? mapVoicingSliderToValue(2, sliders[2]) : 0.35;
  tone.highShelfGainDb = (sliders.size() > 3) ? mapVoicingSliderToValue(3, sliders[3]) : 4.0;
  tone.highShelfFcHz = (sliders.size() > 4) ? mapVoicingSliderToValue(4, sliders[4]) : 2000.0;
  tone.highShelfQ = (sliders.size() > 5) ? mapVoicingSliderToValue(5, sliders[5]) : 0.7;
  tone.voicedTiltDbPerOct = (sliders.size() > 6) ? mapVoicingSliderToValue(6, sliders[6]) : 0.0;
  tone.noiseGlottalModDepth = (sliders.size() > 7) ? mapVoicingSliderToValue(7, sliders[7]) : 0.0;
  tone.pitchSyncF1DeltaHz = (sliders.size() > 8) ? mapVoicingSliderToValue(8, sliders[8]) : 0.0;
  tone.pitchSyncB1DeltaHz = (sliders.size() > 9) ? mapVoicingSliderToValue(9, sliders[9]) : 0.0;
  // V3 params
  tone.speedQuotient = (sliders.size() > 10) ? mapVoicingSliderToValue(10, sliders[10]) : 2.0;
  tone.aspirationTiltDbPerOct = (sliders.size() > 11) ? mapVoicingSliderToValue(11, sliders[11]) : 0.0;
  tone.cascadeBwScale = (sliders.size() > 12) ? mapVoicingSliderToValue(12, sliders[12]) : 1.0;
  tone.tremorDepth = (sliders.size() > 13) ? mapVoicingSliderToValue(13, sliders[13]) : 0.0;
  
  return tone;
}

// Build V1 VoicingTone struct (7 params, no header)
static EditorVoicingToneV1 buildVoicingToneV1(const std::vector<int>& sliders) {
  EditorVoicingToneV1 tone{};
  
  tone.voicingPeakPos = (sliders.size() > 0) ? mapVoicingSliderToValue(0, sliders[0]) : 0.91;
  tone.voicedPreEmphA = (sliders.size() > 1) ? mapVoicingSliderToValue(1, sliders[1]) : 0.92;
  tone.voicedPreEmphMix = (sliders.size() > 2) ? mapVoicingSliderToValue(2, sliders[2]) : 0.35;
  tone.highShelfGainDb = (sliders.size() > 3) ? mapVoicingSliderToValue(3, sliders[3]) : 4.0;
  tone.highShelfFcHz = (sliders.size() > 4) ? mapVoicingSliderToValue(4, sliders[4]) : 2000.0;
  tone.highShelfQ = (sliders.size() > 5) ? mapVoicingSliderToValue(5, sliders[5]) : 0.7;
  tone.voicedTiltDbPerOct = (sliders.size() > 6) ? mapVoicingSliderToValue(6, sliders[6]) : 0.0;
  
  return tone;
}

// Build FrameEx struct from slider values
// Params: creakiness, breathiness, jitter, shimmer (0-100 -> 0.0-1.0)
//         sharpness (0-100 -> 0.5-2.0 multiplier, 50 = 1.0 = default)
static EditorFrameEx buildFrameEx(const std::vector<int>& sliders, bool& outHasEffect) {
  EditorFrameEx ex{};
  outHasEffect = false;
  
  // creakiness: 0-100 -> 0.0-1.0
  int creakVal = (sliders.size() > 0) ? clampInt(sliders[0], 0, 100) : 0;
  ex.creakiness = creakVal / 100.0;
  
  // breathiness: 0-100 -> 0.0-1.0
  int breathVal = (sliders.size() > 1) ? clampInt(sliders[1], 0, 100) : 0;
  ex.breathiness = breathVal / 100.0;
  
  // jitter: 0-100 -> 0.0-1.0
  int jitterVal = (sliders.size() > 2) ? clampInt(sliders[2], 0, 100) : 0;
  ex.jitter = jitterVal / 100.0;
  
  // shimmer: 0-100 -> 0.0-1.0
  int shimmerVal = (sliders.size() > 3) ? clampInt(sliders[3], 0, 100) : 0;
  ex.shimmer = shimmerVal / 100.0;
  
  // sharpness: 0-100 -> 0.5-2.0 multiplier (50 = 1.0 = neutral)
  int sharpVal = (sliders.size() > 4) ? clampInt(sliders[4], 0, 100) : 50;
  ex.sharpness = 0.5 + (sharpVal / 100.0) * 1.5;
  
  // Check if any effect is active (non-default values)
  outHasEffect = (creakVal > 0 || breathVal > 0 || jitterVal > 0 || shimmerVal > 0 || sharpVal != 50);
  
  return ex;
}

// Mix phoneme-level FrameEx values (from YAML) with user defaults.
// Same mixing logic as frontend:
//   - creakiness, breathiness, jitter, shimmer: additive, clamped to [0,1]
//   - sharpness: multiplicative, phoneme >= 1.0 (boost only)
//   - endCf1-3, endPf1-3: absolute Hz values from phoneme (NAN = no ramp)
static EditorFrameEx mixPhonemeFrameEx(const Node& phonemeMap, const EditorFrameEx& userDefaults) {
  EditorFrameEx mixed = userDefaults;
  
  // Look for frameEx: map in the phoneme
  auto fxIt = phonemeMap.map.find("frameEx");
  if (fxIt == phonemeMap.map.end() || !fxIt->second.isMap()) {
    return mixed;  // No per-phoneme overrides
  }
  
  const auto& fxMap = fxIt->second.map;
  
  // Helper to get double from map
  auto getDouble = [&](const std::string& key, double defaultVal) -> double {
    auto it = fxMap.find(key);
    if (it != fxMap.end() && it->second.isScalar()) {
      try { return std::stod(it->second.scalar); }
      catch (...) { return defaultVal; }
    }
    return defaultVal;
  };
  
  // Get phoneme values (0.0 is neutral for additive, 1.0 for multiplicative)
  double phonemeCreakiness = getDouble("creakiness", 0.0);
  double phonemeBreathiness = getDouble("breathiness", 0.0);
  double phonemeJitter = getDouble("jitter", 0.0);
  double phonemeShimmer = getDouble("shimmer", 0.0);
  double phonemeSharpness = getDouble("sharpness", 1.0);
  
  // Phoneme can only BOOST sharpness, not reduce it
  if (phonemeSharpness < 1.0) phonemeSharpness = 1.0;
  
  // Mix: additive for 0-1 params, multiplicative for sharpness
  mixed.creakiness = clampDouble(phonemeCreakiness + userDefaults.creakiness, 0.0, 1.0);
  mixed.breathiness = clampDouble(phonemeBreathiness + userDefaults.breathiness, 0.0, 1.0);
  mixed.jitter = clampDouble(phonemeJitter + userDefaults.jitter, 0.0, 1.0);
  mixed.shimmer = clampDouble(phonemeShimmer + userDefaults.shimmer, 0.0, 1.0);
  mixed.sharpness = clampDouble(phonemeSharpness * userDefaults.sharpness, 0.1, 5.0);
  
  // Formant end targets: absolute Hz values from phoneme (0.0 = no ramp for DSP)
  // These are per-phoneme coarticulation targets, not mixed with user defaults
  mixed.endCf1 = getDouble("endCf1", 0.0);
  mixed.endCf2 = getDouble("endCf2", 0.0);
  mixed.endCf3 = getDouble("endCf3", 0.0);
  mixed.endPf1 = getDouble("endPf1", 0.0);
  mixed.endPf2 = getDouble("endPf2", 0.0);
  mixed.endPf3 = getDouble("endPf3", 0.0);
  
  return mixed;
}

static void applyMul(speechPlayer_frame_t& frame, speechPlayer_frameParam_t speechPlayer_frame_t::* member, double mul) {
  frame.*member = static_cast<speechPlayer_frameParam_t>(static_cast<double>(frame.*member) * mul);
}

static void applyAbs(speechPlayer_frame_t& frame, speechPlayer_frameParam_t speechPlayer_frame_t::* member, double val) {
  frame.*member = static_cast<speechPlayer_frameParam_t>(val);
}

static const FieldMap* findField(const char* name) {
  for (const auto& f : kFieldMap) {
    if (std::strcmp(f.name, name) == 0) return &f;
  }
  return nullptr;
}

void TgsbRuntime::applySpeechSettingsToFrame(speechPlayer_frame_t& frame) const {
  // 1) Voice preset (from NVDA driver's __init__.py)
  // Skip Python presets if using a C++ voice profile (formant transforms already applied by frontend)
  const std::string voice = m_speech.voiceName.empty() ? "Adam" : m_speech.voiceName;
  const bool usingProfile = isVoiceProfile(voice);

  auto mulByName = [&](const char* field, double mul) {
    const FieldMap* f = findField(field);
    if (f) applyMul(frame, f->member, mul);
  };
  auto absByName = [&](const char* field, double val) {
    const FieldMap* f = findField(field);
    if (f) applyAbs(frame, f->member, val);
  };

  if (!usingProfile) {
    // Apply Python voice presets only when NOT using a C++ voice profile
    if (voice == "Adam") {
      mulByName("cb1", 1.3);
      mulByName("pa6", 1.3);
      mulByName("fricationAmplitude", 0.85);
    } else if (voice == "Benjamin") {
      mulByName("cf1", 1.01);
      mulByName("cf2", 1.02);
      absByName("cf4", 3770);
      absByName("cf5", 4100);
      absByName("cf6", 5000);
      mulByName("cfNP", 0.9);
      mulByName("cb1", 1.3);
      mulByName("fricationAmplitude", 0.7);
      mulByName("pa6", 1.3);
    } else if (voice == "Caleb") {
      absByName("aspirationAmplitude", 1);
      absByName("voiceAmplitude", 0);
    } else if (voice == "David") {
      mulByName("voicePitch", 0.75);
      mulByName("endVoicePitch", 0.75);
      mulByName("cf1", 0.75);
      mulByName("cf2", 0.85);
      mulByName("cf3", 0.85);
    } else if (voice == "Robert") {
      // Slightly higher pitch for brighter character
      mulByName("voicePitch", 1.10);
      mulByName("endVoicePitch", 1.10);
      // Moderate formant scaling
      mulByName("cf1", 1.02);
      mulByName("cf2", 1.06);
      mulByName("cf3", 1.08);
      mulByName("cf4", 1.08);
      mulByName("cf5", 1.10);
      mulByName("cf6", 1.05);
      // Narrow bandwidths for buzzy synthetic sound
      mulByName("cb1", 0.65);
      mulByName("cb2", 0.68);
      mulByName("cb3", 0.72);
      mulByName("cb4", 0.75);
      mulByName("cb5", 0.78);
      mulByName("cb6", 0.80);
      // Pressed glottis: sharp, precise attack
      absByName("glottalOpenQuotient", 0.30);
      // Minimal breathiness - clean synthetic sound
      mulByName("voiceTurbulenceAmplitude", 0.20);
      // Increased frication to preserve C, S, F consonants
      mulByName("fricationAmplitude", 0.75);
      // Moderate bypass for consonant clarity
      mulByName("parallelBypass", 0.70);
      // Moderate high parallel formant boost
      mulByName("pa3", 1.08);
      mulByName("pa4", 1.15);
      mulByName("pa5", 1.20);
      mulByName("pa6", 1.25);
      // Moderate parallel bandwidths
      mulByName("pb1", 0.72);
      mulByName("pb2", 0.75);
      mulByName("pb3", 0.78);
      mulByName("pb4", 0.80);
      mulByName("pb5", 0.82);
      mulByName("pb6", 0.85);
      // Match parallel formants to cascade
      mulByName("pf3", 1.06);
      mulByName("pf4", 1.08);
      mulByName("pf5", 1.10);
      mulByName("pf6", 1.05);
      // No vibrato - steady synthetic pitch
      absByName("vibratoPitchOffset", 0.0);
      absByName("vibratoSpeed", 0.0);
    } else {
      // Unknown voice name: fall back to Adam behavior.
      mulByName("cb1", 1.3);
      mulByName("pa6", 1.3);
      mulByName("fricationAmplitude", 0.85);
    }
  }
  // When using a profile, the frontend has already applied formant transforms

  // 2) Per-field multipliers (0..100, 50 => neutral)
  if (m_speech.frameParams.size() == kFieldCount()) {
    for (size_t i = 0; i < kFieldCount(); ++i) {
      const double ratio = static_cast<double>(clampInt(m_speech.frameParams[i], 0, 100)) / 50.0;
      if (ratio == 1.0) continue;
      applyMul(frame, kFieldMap[i].member, ratio);
    }
  }

  // 3) Volume scaling (match NVDA driver: preFormantGain *= volume/75)
  const double vol = static_cast<double>(clampInt(m_speech.volume, 0, 100)) / 75.0;
  frame.preFormantGain = static_cast<speechPlayer_frameParam_t>(static_cast<double>(frame.preFormantGain) * vol);
}

void TgsbRuntime::unload() {
  if (m_feHandle && m_feDestroy) {
    m_feDestroy(m_feHandle);
    m_feHandle = nullptr;
  }

  m_spInitialize = nullptr;
  m_spQueueFrame = nullptr;
  m_spQueueFrameEx = nullptr;
  m_spSynthesize = nullptr;
  m_spTerminate = nullptr;
  m_spSetVoicingTone = nullptr;
  m_spGetDspVersion = nullptr;
  m_voicingToneSupport = VoicingToneSupport::None;

  m_feCreate = nullptr;
  m_feDestroy = nullptr;
  m_feSetLanguage = nullptr;
  m_feQueueIPA = nullptr;
  m_feGetLastError = nullptr;
  m_feSetVoiceProfile = nullptr;
  m_feGetVoiceProfile = nullptr;
  m_feGetPackWarnings = nullptr;
  m_feSetFrameExDefaults = nullptr;
  m_feQueueIPA_Ex = nullptr;

  if (m_frontend) {
    FreeLibrary(m_frontend);
    m_frontend = NULL;
  }
  if (m_speechPlayer) {
    FreeLibrary(m_speechPlayer);
    m_speechPlayer = NULL;
  }
}

bool TgsbRuntime::setDllDirectory(const std::wstring& dllDir, std::string& outError) {
  outError.clear();
  unload();

  if (dllDir.empty()) {
    outError = "DLL directory is empty";
    return false;
  }

  std::wstring spPath = dllDir;
  if (!spPath.empty() && spPath.back() != L'\\' && spPath.back() != L'/') spPath += L'\\';
  std::wstring fePath = spPath;
  spPath += L"speechPlayer.dll";
  fePath += L"nvspFrontend.dll";

  m_speechPlayer = LoadLibraryW(spPath.c_str());
  if (!m_speechPlayer) {
    outError = "Could not load speechPlayer.dll";
    return false;
  }
  m_frontend = LoadLibraryW(fePath.c_str());
  if (!m_frontend) {
    outError = "Could not load nvspFrontend.dll";
    FreeLibrary(m_speechPlayer);
    m_speechPlayer = NULL;
    return false;
  }

  // speechPlayer
  m_spInitialize = reinterpret_cast<sp_initialize_fn>(GetProcAddress(m_speechPlayer, "speechPlayer_initialize"));
  m_spQueueFrame = reinterpret_cast<sp_queueFrame_fn>(GetProcAddress(m_speechPlayer, "speechPlayer_queueFrame"));
  m_spQueueFrameEx = reinterpret_cast<sp_queueFrameEx_fn>(GetProcAddress(m_speechPlayer, "speechPlayer_queueFrameEx"));
  m_spSynthesize = reinterpret_cast<sp_synthesize_fn>(GetProcAddress(m_speechPlayer, "speechPlayer_synthesize"));
  m_spTerminate = reinterpret_cast<sp_terminate_fn>(GetProcAddress(m_speechPlayer, "speechPlayer_terminate"));
  
  // VoicingTone API (optional - 3-tier fallback)
  // Tier 1: No support (oldest DLLs)
  // Tier 2: V1 support (7 params, no header)
  // Tier 3: V2 support (10 params with header)
  m_spSetVoicingTone = reinterpret_cast<sp_setVoicingTone_fn>(GetProcAddress(m_speechPlayer, "speechPlayer_setVoicingTone"));
  m_spGetDspVersion = reinterpret_cast<sp_getDspVersion_fn>(GetProcAddress(m_speechPlayer, "speechPlayer_getDspVersion"));
  
  // Detect VoicingTone support level
  m_voicingToneSupport = VoicingToneSupport::None;
  if (m_spSetVoicingTone) {
    // DLL has setVoicingTone - check if it's v2 (has getDspVersion) or v1 (legacy)
    if (m_spGetDspVersion) {
      // V2 DLL - supports header + 10 params
      m_voicingToneSupport = VoicingToneSupport::V2;
    } else {
      // V1 DLL - legacy 7 params without header
      m_voicingToneSupport = VoicingToneSupport::V1;
    }
  }

  if (!m_spInitialize || !m_spQueueFrame || !m_spSynthesize || !m_spTerminate) {
    outError = "speechPlayer.dll is missing expected exports";
    unload();
    return false;
  }

  // nvspFrontend
  m_feCreate = reinterpret_cast<fe_create_fn>(GetProcAddress(m_frontend, "nvspFrontend_create"));
  m_feDestroy = reinterpret_cast<fe_destroy_fn>(GetProcAddress(m_frontend, "nvspFrontend_destroy"));
  m_feSetLanguage = reinterpret_cast<fe_setLanguage_fn>(GetProcAddress(m_frontend, "nvspFrontend_setLanguage"));
  m_feQueueIPA = reinterpret_cast<fe_queueIPA_fn>(GetProcAddress(m_frontend, "nvspFrontend_queueIPA"));
  m_feGetLastError = reinterpret_cast<fe_getLastError_fn>(GetProcAddress(m_frontend, "nvspFrontend_getLastError"));
  
  // Voice profile API (optional - may not be present in older DLLs)
  m_feSetVoiceProfile = reinterpret_cast<fe_setVoiceProfile_fn>(GetProcAddress(m_frontend, "nvspFrontend_setVoiceProfile"));
  m_feGetVoiceProfile = reinterpret_cast<fe_getVoiceProfile_fn>(GetProcAddress(m_frontend, "nvspFrontend_getVoiceProfile"));
  m_feGetPackWarnings = reinterpret_cast<fe_getPackWarnings_fn>(GetProcAddress(m_frontend, "nvspFrontend_getPackWarnings"));
  
  // FrameEx API (optional - enables per-phoneme voice quality mixing)
  m_feSetFrameExDefaults = reinterpret_cast<fe_setFrameExDefaults_fn>(GetProcAddress(m_frontend, "nvspFrontend_setFrameExDefaults"));
  m_feQueueIPA_Ex = reinterpret_cast<fe_queueIPA_Ex_fn>(GetProcAddress(m_frontend, "nvspFrontend_queueIPA_Ex"));

  if (!m_feCreate || !m_feDestroy || !m_feSetLanguage || !m_feQueueIPA || !m_feGetLastError) {
    outError = "nvspFrontend.dll is missing expected exports";
    unload();
    return false;
  }

  return true;
}

bool TgsbRuntime::setPackRoot(const std::wstring& packRootDir, std::string& outError) {
  outError.clear();
  m_packRoot = packRootDir;

  // Reset the frontend handle; it is tied to packDir.
  if (m_feHandle && m_feDestroy) {
    m_feDestroy(m_feHandle);
    m_feHandle = nullptr;
  }

  return true;
}

bool TgsbRuntime::setLanguage(const std::string& langTagUtf8, std::string& outError) {
  outError.clear();
  m_langTag = langTagUtf8;

  if (!dllsLoaded()) {
    outError = "DLLs are not loaded";
    return false;
  }
  if (m_packRoot.empty()) {
    outError = "Pack root is not set";
    return false;
  }

  if (!m_feHandle) {
    std::string packUtf8 = wideToUtf8(m_packRoot);
    m_feHandle = m_feCreate(packUtf8.c_str());
    if (!m_feHandle) {
      outError = "nvspFrontend_create failed (check packs/phonemes.yaml)";
      return false;
    }
  }

  if (!langTagUtf8.empty()) {
    int ok = m_feSetLanguage(m_feHandle, langTagUtf8.c_str());
    if (!ok) {
      const char* msg = m_feGetLastError(m_feHandle);
      m_lastFrontendError = msg ? msg : "";
      outError = m_lastFrontendError.empty() ? "nvspFrontend_setLanguage failed" : m_lastFrontendError;
      return false;
    }
  }

  return true;
}

bool TgsbRuntime::dllsLoaded() const {
  return m_speechPlayer && m_frontend && m_spInitialize && m_spQueueFrame && m_spSynthesize && m_spTerminate && m_feCreate;
}

static bool synthesizeAll(
  sp_synthesize_fn synthFn,
  speechPlayer_handle_t player,
  std::vector<sample>& outSamples
) {
  const unsigned int block = 2048;
  std::vector<sample> tmp(block);

  while (true) {
    int n = synthFn(player, block, tmp.data());
    if (n <= 0) break;
    outSamples.insert(outSamples.end(), tmp.begin(), tmp.begin() + n);
    if (static_cast<unsigned int>(n) < block) break;
  }
  return true;
}

bool TgsbRuntime::synthPreviewPhoneme(
  const Node& phonemeMap,
  int sampleRate,
  std::vector<sample>& outSamples,
  std::string& outError
) {
  outSamples.clear();
  outError.clear();

  if (!dllsLoaded()) {
    outError = "DLLs are not loaded";
    return false;
  }

  speechPlayer_handle_t player = m_spInitialize(sampleRate);
  if (!player) {
    outError = "speechPlayer_initialize failed";
    return false;
  }

  // Apply VoicingTone if DLL supports it (3-tier fallback)
  if (!m_speech.voicingParams.empty()) {
    switch (m_voicingToneSupport) {
      case VoicingToneSupport::V2: {
        EditorVoicingToneV2 tone = buildVoicingToneV2(m_speech.voicingParams);
        m_spSetVoicingTone(player, &tone);
        break;
      }
      case VoicingToneSupport::V1: {
        EditorVoicingToneV1 tone = buildVoicingToneV1(m_speech.voicingParams);
        m_spSetVoicingTone(player, &tone);
        break;
      }
      case VoicingToneSupport::None:
      default:
        // DLL doesn't support VoicingTone - skip silently
        break;
    }
  }

  speechPlayer_frame_t frame{};
  bool isVowel = false;
  applyPhonemeMapToFrame(phonemeMap, frame, isVowel);
  applySpeechSettingsToFrame(frame);

  const double preMs = 35.0;
  const double durMs = isVowel ? 180.0 : 120.0;
  const double postMs = 50.0;
  const double fadeMs = 8.0;

  const unsigned int preS = msToSamples(preMs, sampleRate);
  const unsigned int durS = msToSamples(durMs, sampleRate);
  const unsigned int postS = msToSamples(postMs, sampleRate);
  const unsigned int fadeS = msToSamples(fadeMs, sampleRate);

  // Build FrameEx: start with user defaults, then mix in per-phoneme values
  bool hasFrameEx = false;
  EditorFrameEx userFrameEx = buildFrameEx(m_speech.frameExParams, hasFrameEx);
  EditorFrameEx frameEx = mixPhonemeFrameEx(phonemeMap, userFrameEx);
  
  // Always use FrameEx if the phoneme has values OR user has values
  auto fxIt = phonemeMap.map.find("frameEx");
  bool phonemeHasFrameEx = (fxIt != phonemeMap.map.end() && fxIt->second.isMap());
  hasFrameEx = hasFrameEx || phonemeHasFrameEx;

  // Purge on first queue.
  if (m_spQueueFrameEx && hasFrameEx) {
    m_spQueueFrameEx(player, nullptr, &frameEx, static_cast<unsigned int>(sizeof(EditorFrameEx)), preS, fadeS, -1, true);
    m_spQueueFrameEx(player, &frame, &frameEx, static_cast<unsigned int>(sizeof(EditorFrameEx)), durS, fadeS, -1, false);
    m_spQueueFrameEx(player, nullptr, &frameEx, static_cast<unsigned int>(sizeof(EditorFrameEx)), postS, fadeS, -1, false);
  } else {
    m_spQueueFrame(player, nullptr, preS, fadeS, -1, true);
    m_spQueueFrame(player, &frame, durS, fadeS, -1, false);
    m_spQueueFrame(player, nullptr, postS, fadeS, -1, false);
  }

  synthesizeAll(m_spSynthesize, player, outSamples);
  m_spTerminate(player);
  return true;
}

// -------------------------
// Punctuation pauses (matches NVDA driver behavior)
// -------------------------

static std::string toLowerAscii(std::string s) {
  for (auto& ch : s) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
  return s;
}

static double punctuationPauseMs(char punct, const std::string& pauseMode) {
  const std::string pm = toLowerAscii(pauseMode);
  if (pm == "off") return 0.0;

  auto strong = [&]() { return (pm == "long") ? 50.0 : 30.0; };
  auto comma = [&]() { return (pm == "long") ? 6.0 : 0.0; };

  switch (punct) {
    case '.':
    case '!':
    case '?':
      return strong();
    case ':':
    case ';':
      return strong();
    case ',':
      return comma();
    default:
      return 0.0;
  }
}

static bool isClauseMarkerToken(const std::string& tok, char& outPunct) {
  // Marker tokens are inserted by the phonemizer bridge and may also be typed
  // directly by users in IPA mode.
  //
  // Supported: ".", "!", "?", ",", ":", ";", "..." (ellipsis treated as '.').
  if (tok == "...") {
    outPunct = '.';
    return true;
  }
  if (tok.size() == 1) {
    const char c = tok[0];
    if (c == '.' || c == '!' || c == '?' || c == ',' || c == ':' || c == ';') {
      outPunct = c;
      return true;
    }
  }
  return false;
}

struct IpaClauseChunk {
  std::string ipa;   // IPA tokens (no marker punctuation tokens)
  char punct = 0;    // punctuation that ended this chunk (0 if none)
};

static void splitIpaByClauseMarkers(const std::string& ipaUtf8, std::vector<IpaClauseChunk>& out) {
  out.clear();

  // Tokenize on ASCII whitespace. (IPA itself can include non-ASCII bytes.)
  std::vector<std::string> tokens;
  tokens.reserve(256);
  std::string cur;
  cur.reserve(64);

  auto flushTok = [&]() {
    if (!cur.empty()) {
      tokens.push_back(std::move(cur));
      cur.clear();
    }
  };

  for (unsigned char b : ipaUtf8) {
    if (b == ' ' || b == '\t' || b == '\r' || b == '\n' || b == '\v' || b == '\f') {
      flushTok();
      continue;
    }
    cur.push_back(static_cast<char>(b));
  }
  flushTok();

  std::vector<std::string> buf;
  buf.reserve(tokens.size());

  auto flushClause = [&](char punct) {
    if (buf.empty()) return;
    std::string joined;
    // Rebuild with single spaces.
    size_t total = 0;
    for (const auto& t : buf) total += t.size() + 1;
    joined.reserve(total);

    for (size_t i = 0; i < buf.size(); ++i) {
      if (i) joined.push_back(' ');
      joined += buf[i];
    }

    IpaClauseChunk c;
    c.ipa = std::move(joined);
    c.punct = punct;
    out.push_back(std::move(c));
    buf.clear();
  };

  for (const auto& t : tokens) {
    char punct = 0;
    if (isClauseMarkerToken(t, punct)) {
      flushClause(punct);
      continue;
    }
    buf.push_back(t);
  }
  flushClause(0);

  // If nothing was split out (e.g. whitespace-only), keep a single empty chunk.
  if (out.empty()) {
    IpaClauseChunk c;
    c.ipa = "";
    c.punct = 0;
    out.push_back(std::move(c));
  }
}

struct QueueCtx {
  sp_queueFrame_fn queueFrame;
  sp_queueFrameEx_fn queueFrameEx;
  EditorFrameEx frameEx;
  bool hasFrameEx;
  speechPlayer_handle_t player;
  int sampleRate;
  bool first;
  const TgsbRuntime* runtime;
};

static void __cdecl frameCallback(
  void* userData,
  const nvspFrontend_Frame* frameOrNull,
  double durationMs,
  double fadeMs,
  int userIndex
) {
  QueueCtx* ctx = reinterpret_cast<QueueCtx*>(userData);
  if (!ctx || (!ctx->queueFrame && !ctx->queueFrameEx)) return;

  unsigned int durS = msToSamples(durationMs, ctx->sampleRate);
  unsigned int fadeS = msToSamples(fadeMs, ctx->sampleRate);

  if (frameOrNull) {
    speechPlayer_frame_t f{};
    // Copy field-by-field (no ABI/layout assumptions).
    f.voicePitch = frameOrNull->voicePitch;
    f.vibratoPitchOffset = frameOrNull->vibratoPitchOffset;
    f.vibratoSpeed = frameOrNull->vibratoSpeed;
    f.voiceTurbulenceAmplitude = frameOrNull->voiceTurbulenceAmplitude;
    f.glottalOpenQuotient = frameOrNull->glottalOpenQuotient;
    f.voiceAmplitude = frameOrNull->voiceAmplitude;
    f.aspirationAmplitude = frameOrNull->aspirationAmplitude;

    f.cf1 = frameOrNull->cf1;
    f.cf2 = frameOrNull->cf2;
    f.cf3 = frameOrNull->cf3;
    f.cf4 = frameOrNull->cf4;
    f.cf5 = frameOrNull->cf5;
    f.cf6 = frameOrNull->cf6;
    f.cfN0 = frameOrNull->cfN0;
    f.cfNP = frameOrNull->cfNP;

    f.cb1 = frameOrNull->cb1;
    f.cb2 = frameOrNull->cb2;
    f.cb3 = frameOrNull->cb3;
    f.cb4 = frameOrNull->cb4;
    f.cb5 = frameOrNull->cb5;
    f.cb6 = frameOrNull->cb6;
    f.cbN0 = frameOrNull->cbN0;
    f.cbNP = frameOrNull->cbNP;

    f.caNP = frameOrNull->caNP;

    f.fricationAmplitude = frameOrNull->fricationAmplitude;

    f.pf1 = frameOrNull->pf1;
    f.pf2 = frameOrNull->pf2;
    f.pf3 = frameOrNull->pf3;
    f.pf4 = frameOrNull->pf4;
    f.pf5 = frameOrNull->pf5;
    f.pf6 = frameOrNull->pf6;

    f.pb1 = frameOrNull->pb1;
    f.pb2 = frameOrNull->pb2;
    f.pb3 = frameOrNull->pb3;
    f.pb4 = frameOrNull->pb4;
    f.pb5 = frameOrNull->pb5;
    f.pb6 = frameOrNull->pb6;

    f.pa1 = frameOrNull->pa1;
    f.pa2 = frameOrNull->pa2;
    f.pa3 = frameOrNull->pa3;
    f.pa4 = frameOrNull->pa4;
    f.pa5 = frameOrNull->pa5;
    f.pa6 = frameOrNull->pa6;

    f.parallelBypass = frameOrNull->parallelBypass;
    f.preFormantGain = frameOrNull->preFormantGain;
    f.outputGain = frameOrNull->outputGain;
    f.endVoicePitch = frameOrNull->endVoicePitch;

    if (ctx->runtime) {
      ctx->runtime->applySpeechSettingsToFrame(f);
    }

    // Use queueFrameEx if available and we have FrameEx params
    if (ctx->queueFrameEx && ctx->hasFrameEx) {
      ctx->queueFrameEx(ctx->player, &f, &ctx->frameEx, 
                        static_cast<unsigned int>(sizeof(EditorFrameEx)),
                        durS, fadeS, userIndex, ctx->first);
    } else if (ctx->queueFrame) {
      ctx->queueFrame(ctx->player, &f, durS, fadeS, userIndex, ctx->first);
    }
  } else {
    // Silence frame
    if (ctx->queueFrameEx && ctx->hasFrameEx) {
      ctx->queueFrameEx(ctx->player, nullptr, &ctx->frameEx,
                        static_cast<unsigned int>(sizeof(EditorFrameEx)),
                        durS, fadeS, userIndex, ctx->first);
    } else if (ctx->queueFrame) {
      ctx->queueFrame(ctx->player, nullptr, durS, fadeS, userIndex, ctx->first);
    }
  }
  ctx->first = false;
}

// New callback for queueIPA_Ex - receives MIXED FrameEx from frontend
// (phoneme values + user defaults already combined)
static void __cdecl frameExCallback(
  void* userData,
  const nvspFrontend_Frame* frameOrNull,
  const nvspFrontend_FrameEx* frameExOrNull,
  double durationMs,
  double fadeMs,
  int userIndex
) {
  QueueCtx* ctx = reinterpret_cast<QueueCtx*>(userData);
  if (!ctx || (!ctx->queueFrame && !ctx->queueFrameEx)) return;

  unsigned int durS = msToSamples(durationMs, ctx->sampleRate);
  unsigned int fadeS = msToSamples(fadeMs, ctx->sampleRate);

  if (frameOrNull) {
    speechPlayer_frame_t f{};
    // Copy field-by-field (no ABI/layout assumptions).
    f.voicePitch = frameOrNull->voicePitch;
    f.vibratoPitchOffset = frameOrNull->vibratoPitchOffset;
    f.vibratoSpeed = frameOrNull->vibratoSpeed;
    f.voiceTurbulenceAmplitude = frameOrNull->voiceTurbulenceAmplitude;
    f.glottalOpenQuotient = frameOrNull->glottalOpenQuotient;
    f.voiceAmplitude = frameOrNull->voiceAmplitude;
    f.aspirationAmplitude = frameOrNull->aspirationAmplitude;

    f.cf1 = frameOrNull->cf1;
    f.cf2 = frameOrNull->cf2;
    f.cf3 = frameOrNull->cf3;
    f.cf4 = frameOrNull->cf4;
    f.cf5 = frameOrNull->cf5;
    f.cf6 = frameOrNull->cf6;
    f.cfN0 = frameOrNull->cfN0;
    f.cfNP = frameOrNull->cfNP;

    f.cb1 = frameOrNull->cb1;
    f.cb2 = frameOrNull->cb2;
    f.cb3 = frameOrNull->cb3;
    f.cb4 = frameOrNull->cb4;
    f.cb5 = frameOrNull->cb5;
    f.cb6 = frameOrNull->cb6;
    f.cbN0 = frameOrNull->cbN0;
    f.cbNP = frameOrNull->cbNP;

    f.caNP = frameOrNull->caNP;

    f.fricationAmplitude = frameOrNull->fricationAmplitude;

    f.pf1 = frameOrNull->pf1;
    f.pf2 = frameOrNull->pf2;
    f.pf3 = frameOrNull->pf3;
    f.pf4 = frameOrNull->pf4;
    f.pf5 = frameOrNull->pf5;
    f.pf6 = frameOrNull->pf6;

    f.pb1 = frameOrNull->pb1;
    f.pb2 = frameOrNull->pb2;
    f.pb3 = frameOrNull->pb3;
    f.pb4 = frameOrNull->pb4;
    f.pb5 = frameOrNull->pb5;
    f.pb6 = frameOrNull->pb6;

    f.pa1 = frameOrNull->pa1;
    f.pa2 = frameOrNull->pa2;
    f.pa3 = frameOrNull->pa3;
    f.pa4 = frameOrNull->pa4;
    f.pa5 = frameOrNull->pa5;
    f.pa6 = frameOrNull->pa6;

    f.parallelBypass = frameOrNull->parallelBypass;
    f.preFormantGain = frameOrNull->preFormantGain;
    f.outputGain = frameOrNull->outputGain;
    f.endVoicePitch = frameOrNull->endVoicePitch;

    if (ctx->runtime) {
      ctx->runtime->applySpeechSettingsToFrame(f);
    }

    // Use the MIXED FrameEx from frontend (phoneme + user defaults + Fujisaki pitch)
    if (ctx->queueFrameEx && frameExOrNull) {
      EditorFrameEx mixedEx{};
      // Copy all 23 fields - includes formant ramping, Fujisaki pitch model, transition scales, and amplitude mode
      static_assert(sizeof(EditorFrameEx) == sizeof(nvspFrontend_FrameEx), "EditorFrameEx size mismatch");
      std::memcpy(&mixedEx, frameExOrNull, sizeof(EditorFrameEx));
      ctx->queueFrameEx(ctx->player, &f, &mixedEx, 
                        static_cast<unsigned int>(sizeof(EditorFrameEx)),
                        durS, fadeS, userIndex, ctx->first);
    } else if (ctx->queueFrameEx && ctx->hasFrameEx) {
      // Fallback: use ctx->frameEx if frontend didn't provide mixed values
      ctx->queueFrameEx(ctx->player, &f, &ctx->frameEx, 
                        static_cast<unsigned int>(sizeof(EditorFrameEx)),
                        durS, fadeS, userIndex, ctx->first);
    } else if (ctx->queueFrame) {
      ctx->queueFrame(ctx->player, &f, durS, fadeS, userIndex, ctx->first);
    }
  } else {
    // Silence frame - use ctx->frameEx for continuity (frontend gives NULL for silence)
    if (ctx->queueFrameEx && ctx->hasFrameEx) {
      ctx->queueFrameEx(ctx->player, nullptr, &ctx->frameEx,
                        static_cast<unsigned int>(sizeof(EditorFrameEx)),
                        durS, fadeS, userIndex, ctx->first);
    } else if (ctx->queueFrame) {
      ctx->queueFrame(ctx->player, nullptr, durS, fadeS, userIndex, ctx->first);
    }
  }
  ctx->first = false;
}

bool TgsbRuntime::synthIpa(
  const std::string& ipaUtf8,
  int sampleRate,
  std::vector<sample>& outSamples,
  std::string& outError
) {
  outSamples.clear();
  outError.clear();
  m_lastFrontendError.clear();

  if (!dllsLoaded()) {
    outError = "DLLs are not loaded";
    return false;
  }
  if (m_packRoot.empty()) {
    outError = "Pack root is not set";
    return false;
  }

  // Ensure frontend handle + language.
  if (!m_feHandle) {
    std::string packUtf8 = wideToUtf8(m_packRoot);
    m_feHandle = m_feCreate(packUtf8.c_str());
    if (!m_feHandle) {
      outError = "nvspFrontend_create failed (check packs/phonemes.yaml)";
      return false;
    }
  }
  if (!m_langTag.empty()) {
    int ok = m_feSetLanguage(m_feHandle, m_langTag.c_str());
    if (!ok) {
      const char* msg = m_feGetLastError(m_feHandle);
      m_lastFrontendError = msg ? msg : "";
      outError = m_lastFrontendError.empty() ? "nvspFrontend_setLanguage failed" : m_lastFrontendError;
      return false;
    }
  }
  
  // Set voice profile if using one
  if (m_feSetVoiceProfile) {
    const std::string& voice = m_speech.voiceName;
    if (isVoiceProfile(voice)) {
      std::string profileName = getProfileNameFromVoice(voice);
      m_feSetVoiceProfile(m_feHandle, profileName.c_str());
    } else {
      // Clear any active profile when using Python presets
      m_feSetVoiceProfile(m_feHandle, "");
    }
  }

  speechPlayer_handle_t player = m_spInitialize(sampleRate);
  if (!player) {
    outError = "speechPlayer_initialize failed";
    return false;
  }

  // Apply VoicingTone if DLL supports it (3-tier fallback)
  if (!m_speech.voicingParams.empty()) {
    switch (m_voicingToneSupport) {
      case VoicingToneSupport::V2: {
        EditorVoicingToneV2 tone = buildVoicingToneV2(m_speech.voicingParams);
        m_spSetVoicingTone(player, &tone);
        break;
      }
      case VoicingToneSupport::V1: {
        EditorVoicingToneV1 tone = buildVoicingToneV1(m_speech.voicingParams);
        m_spSetVoicingTone(player, &tone);
        break;
      }
      case VoicingToneSupport::None:
      default:
        // DLL doesn't support VoicingTone - skip silently
        break;
    }
  }

  QueueCtx ctx{};
  ctx.queueFrame = m_spQueueFrame;
  ctx.queueFrameEx = m_spQueueFrameEx;
  ctx.frameEx = buildFrameEx(m_speech.frameExParams, ctx.hasFrameEx);
  ctx.player = player;
  ctx.sampleRate = sampleRate;
  ctx.first = true;
  ctx.runtime = this;

  // Set FrameEx defaults on frontend for per-phoneme mixing.
  // This lets the frontend combine user slider values with per-phoneme YAML values.
  // Always set defaults when using the new API (even zeros), so frontend has correct state.
  if (m_feSetFrameExDefaults) {
    m_feSetFrameExDefaults(
      m_feHandle,
      ctx.frameEx.creakiness,
      ctx.frameEx.breathiness,
      ctx.frameEx.jitter,
      ctx.frameEx.shimmer,
      ctx.frameEx.sharpness
    );
  }

  // Match NVDA driver's mapping:
  //   rate: 0..100 -> 0.25 * 2^(rate/25)
  //   pitch: 0..100 -> basePitch = 25 + 21.25*(pitch/12.5)
  //   inflection: 0..100 -> 0.0..1.0
  const int rate = clampInt(m_speech.rate, 0, 100);
  const int pitch = clampInt(m_speech.pitch, 0, 100);
  const int infl = clampInt(m_speech.inflection, 0, 100);
  const double speed = 0.25 * std::pow(2.0, static_cast<double>(rate) / 25.0);
  const double basePitch = 25.0 + (21.25 * (static_cast<double>(pitch) / 12.5));
  const double inflection = static_cast<double>(infl) / 100.0;

// Split IPA into clause chunks. Clause markers can be:
// - inserted by our phonemizer chunker (for text->IPA)
// - typed directly by users (in IPA mode)
//
// This lets us insert real silence frames between sentences/clauses so speech
// does not sound like one long run-on stream.
std::vector<IpaClauseChunk> clauses;
splitIpaByClauseMarkers(ipaUtf8, clauses);

bool ok = true;
for (size_t i = 0; i < clauses.size(); ++i) {
  const IpaClauseChunk& c = clauses[i];
  if (c.ipa.empty()) continue;

  // nvspFrontend only reads a single byte from clauseType.
  // Use '.' as a safe default if the chunk has no marker punctuation.
  const char punct = c.punct ? c.punct : '.';
  char clauseBuf[2] = { punct, 0 };

  int qok;
  // Use queueIPA_Ex if available - this gets per-phoneme FrameEx mixing from frontend
  if (m_feQueueIPA_Ex) {
    qok = m_feQueueIPA_Ex(
      m_feHandle,
      c.ipa.c_str(),
      speed,
      basePitch,
      inflection,
      clauseBuf,
      -1,
      frameExCallback,
      &ctx
    );
  } else {
    // Fallback to old API (no per-phoneme FrameEx mixing)
    qok = m_feQueueIPA(
      m_feHandle,
      c.ipa.c_str(),
      speed,
      basePitch,
      inflection,
      clauseBuf,
      -1,
      frameCallback,
      &ctx
    );
  }

  if (!qok) {
    ok = false;
    break;
  }

  // Optional punctuation pause (micro-silence) between clauses.
  // This is separate from "clauseType" prosody; it adds actual time separation.
  if (c.punct && (i + 1) < clauses.size()) {
    const double pMs = punctuationPauseMs(c.punct, m_speech.pauseMode);
    if (pMs > 0.0) {
      const unsigned int durS = msToSamples(pMs, sampleRate);
      const double fadeMs = (std::min)(pMs, 3.0);
      const unsigned int fadeS = msToSamples(fadeMs, sampleRate);
      if (ctx.queueFrameEx && ctx.hasFrameEx) {
        ctx.queueFrameEx(ctx.player, nullptr, &ctx.frameEx,
                         static_cast<unsigned int>(sizeof(EditorFrameEx)),
                         durS, fadeS, -1, ctx.first);
      } else if (ctx.queueFrame) {
        ctx.queueFrame(ctx.player, nullptr, durS, fadeS, -1, ctx.first);
      }
      ctx.first = false;
    }
  }
}

if (!ok) {
    const char* msg = m_feGetLastError(m_feHandle);
    m_lastFrontendError = msg ? msg : "";
    outError = m_lastFrontendError.empty() ? "nvspFrontend_queueIPA failed" : m_lastFrontendError;
    m_spTerminate(player);
    return false;
  }

  synthesizeAll(m_spSynthesize, player, outSamples);
  m_spTerminate(player);
  return true;
}

// -----------------------------------------------------------------------------
// Voice profile support
// -----------------------------------------------------------------------------

bool TgsbRuntime::isVoiceProfile(const std::string& voiceName) {
  return voiceName.rfind(kVoiceProfilePrefix, 0) == 0;
}

std::string TgsbRuntime::getProfileNameFromVoice(const std::string& voiceName) {
  if (!isVoiceProfile(voiceName)) return "";
  return voiceName.substr(std::strlen(kVoiceProfilePrefix));
}

std::vector<std::string> TgsbRuntime::discoverVoiceProfiles() const {
  std::vector<std::string> profiles;
  
  if (m_packRoot.empty()) return profiles;
  
  // Build path to phonemes.yaml
  // Note: m_packRoot is the 'packs' directory itself (set via runtimePackDir)
  std::wstring yamlPath = m_packRoot;
  if (!yamlPath.empty() && yamlPath.back() != L'\\' && yamlPath.back() != L'/') {
    yamlPath += L'\\';
  }
  yamlPath += L"phonemes.yaml";
  
  // Simple line-based parser to extract profile names from voiceProfiles: section
  // Supports both nested format and dotted-key format:
  //   Nested:  female:
  //              classScales:
  //   Dotted:  female.classScales.vowel.cf_mul: [...]
  std::ifstream f(yamlPath);
  if (!f.is_open()) return profiles;
  
  bool inVoiceProfiles = false;
  int baseIndent = -1;
  std::unordered_set<std::string> seenProfiles;  // Avoid duplicates
  std::string line;
  
  while (std::getline(f, line)) {
    // Strip trailing CR if present (Windows line endings)
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    
    // Skip empty lines
    std::string stripped = line;
    size_t firstNonSpace = stripped.find_first_not_of(" \t");
    if (firstNonSpace == std::string::npos) continue;
    stripped = stripped.substr(firstNonSpace);
    
    // Skip comments
    if (!stripped.empty() && stripped[0] == '#') continue;
    
    // Check for voiceProfiles: at column 0
    if (line[0] != ' ' && line[0] != '\t') {
      if (stripped.rfind("voiceProfiles:", 0) == 0) {
        inVoiceProfiles = true;
        baseIndent = -1;
        continue;
      } else if (inVoiceProfiles) {
        // Left the section (back to column 0)
        break;
      }
    }
    
    if (inVoiceProfiles) {
      // Count indent
      int indent = 0;
      for (char c : line) {
        if (c == ' ') indent++;
        else if (c == '\t') indent += 2;
        else break;
      }
      
      if (baseIndent < 0) {
        baseIndent = indent;
      }
      
      // Profile names are at base indent level and end with ':'
      if (indent == baseIndent) {
        size_t colonPos = stripped.find(':');
        if (colonPos != std::string::npos) {
          std::string key = stripped.substr(0, colonPos);
          // Trim whitespace from key
          size_t keyEnd = key.find_last_not_of(" \t");
          if (keyEnd != std::string::npos) {
            key = key.substr(0, keyEnd + 1);
          }
          
          if (!key.empty() && key[0] != '#') {
            // For dotted keys like "female.classScales.vowel.cf_mul",
            // extract just the first part "female" as the profile name
            size_t dotPos = key.find('.');
            if (dotPos != std::string::npos) {
              key = key.substr(0, dotPos);
            }
            
            // Only add if we haven't seen this profile yet
            if (!key.empty() && seenProfiles.find(key) == seenProfiles.end()) {
              seenProfiles.insert(key);
              profiles.push_back(key);
            }
          }
        }
      }
    }
  }
  
  return profiles;
}

bool TgsbRuntime::setVoiceProfile(const std::string& profileName, std::string& outError) {
  outError.clear();
  
  if (!m_feHandle) {
    outError = "Frontend not initialized";
    return false;
  }
  
  if (!m_feSetVoiceProfile) {
    // API not available - older DLL version
    outError = "Voice profile API not available (DLL too old?)";
    return false;
  }
  
  int ok = m_feSetVoiceProfile(m_feHandle, profileName.c_str());
  if (!ok) {
    const char* msg = m_feGetLastError ? m_feGetLastError(m_feHandle) : nullptr;
    outError = msg ? msg : "setVoiceProfile failed";
    return false;
  }
  
  return true;
}

std::string TgsbRuntime::getVoiceProfile() const {
  if (!m_feHandle || !m_feGetVoiceProfile) return "";
  
  const char* name = m_feGetVoiceProfile(m_feHandle);
  return name ? name : "";
}

bool TgsbRuntime::saveVoiceProfileSliders(const std::string& profileName,
                                          const std::vector<int>& voicingSliders,
                                          const std::vector<int>& frameExSliders,
                                          std::string& outError) {
  outError.clear();
  
  if (m_packRoot.empty()) {
    outError = "No pack loaded. Open a pack root first (File > Open Pack Root).";
    return false;
  }
  
  // Build path to phonemes.yaml
  // m_packRoot could be either:
  //   - The packs folder itself (app.packsDir): C:\git\TGSpeechBox\packs
  //   - Or the parent (packRoot for frontend): C:\git\TGSpeechBox
  // Try both patterns
  std::wstring yamlPath = m_packRoot;
  if (!yamlPath.empty() && yamlPath.back() != L'\\' && yamlPath.back() != L'/') {
    yamlPath += L'\\';
  }
  yamlPath += L"phonemes.yaml";
  
  // If that doesn't exist, try packs/phonemes.yaml
  {
    std::ifstream test(yamlPath);
    if (!test.is_open()) {
      yamlPath = m_packRoot;
      if (!yamlPath.empty() && yamlPath.back() != L'\\' && yamlPath.back() != L'/') {
        yamlPath += L'\\';
      }
      yamlPath += L"packs\\phonemes.yaml";
    }
  }
  
  // Load existing profiles
  std::vector<VPVoiceProfile> profiles;
  std::string loadErr;
  if (!loadVoiceProfilesFromYaml(yamlPath, profiles, loadErr)) {
    // It's OK if file doesn't exist or has no profiles - we'll create one
    profiles.clear();
  }
  
  // Find or create the target profile
  VPVoiceProfile* targetProfile = nullptr;
  for (auto& p : profiles) {
    if (p.name == profileName) {
      targetProfile = &p;
      break;
    }
  }
  
  if (!targetProfile) {
    // Create new profile
    profiles.push_back(VPVoiceProfile{});
    targetProfile = &profiles.back();
    targetProfile->name = profileName;
  }
  
  // Build voicingTone map with all params
  targetProfile->hasVoicingTone = true;
  targetProfile->voicingTone.clear();
  
  // Helper to format double as string
  auto fmtDouble = [](double v) -> std::string {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << v;
    std::string s = oss.str();
    // Trim trailing zeros after decimal point
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
      size_t last = s.find_last_not_of('0');
      if (last != std::string::npos && last > dot) {
        s = s.substr(0, last + 1);
      }
      // Remove trailing dot
      if (s.back() == '.') s.pop_back();
    }
    return s;
  };
  
  // 14 VoicingTone params
  const auto& voicingNames = voicingParamNames();
  for (size_t i = 0; i < voicingNames.size() && i < voicingSliders.size(); ++i) {
    double val = mapVoicingSliderToValue(static_cast<int>(i), voicingSliders[i]);
    targetProfile->voicingTone[voicingNames[i]] = fmtDouble(val);
  }
  
  // 5 FrameEx params
  const auto& frameExNames = frameExParamNames();
  auto clamp01 = [](int v) -> double {
    int c = (v < 0) ? 0 : ((v > 100) ? 100 : v);
    return static_cast<double>(c) / 100.0;
  };
  
  if (frameExSliders.size() > 0 && frameExNames.size() > 0)
    targetProfile->voicingTone[frameExNames[0]] = fmtDouble(clamp01(frameExSliders[0]));  // creakiness
  if (frameExSliders.size() > 1 && frameExNames.size() > 1)
    targetProfile->voicingTone[frameExNames[1]] = fmtDouble(clamp01(frameExSliders[1]));  // breathiness
  if (frameExSliders.size() > 2 && frameExNames.size() > 2)
    targetProfile->voicingTone[frameExNames[2]] = fmtDouble(clamp01(frameExSliders[2]));  // jitter
  if (frameExSliders.size() > 3 && frameExNames.size() > 3)
    targetProfile->voicingTone[frameExNames[3]] = fmtDouble(clamp01(frameExSliders[3]));  // shimmer
  if (frameExSliders.size() > 4 && frameExNames.size() > 4) {
    // sharpness: 0-100 -> 0.5-2.0
    int sharpVal = frameExSliders[4];
    sharpVal = (sharpVal < 0) ? 0 : ((sharpVal > 100) ? 100 : sharpVal);
    double sharpness = 0.5 + (static_cast<double>(sharpVal) / 100.0) * 1.5;
    targetProfile->voicingTone[frameExNames[4]] = fmtDouble(sharpness);
  }
  
  // Save back to YAML
  if (!saveVoiceProfilesToYaml(yamlPath, profiles, outError)) {
    return false;
  }
  
  return true;
}

} // namespace tgsb_editor
