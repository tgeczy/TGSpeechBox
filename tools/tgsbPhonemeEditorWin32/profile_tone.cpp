/*
TGSpeechBox — Voice profiles as the phoneme editor holds them, and "Save to Profile".
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "profile_tone.h"
#include "voicing_sliders.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <initializer_list>
#include <sstream>

namespace tgsb_editor {

static std::string trim(const std::string& s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

static bool parseDouble(const std::string& s, double& out) {
  try {
    size_t pos;
    out = std::stod(s, &pos);
    return pos > 0;
  } catch (...) {
    return false;
  }
}

// ---------------------------------------------------------------------------
// Voice source of a profile (see profile_tone.h)


const char* const kProfileToneKeys[kProfileToneKeyCount] = {
  "voicingPeakPos", "voicedPreEmphA", "voicedPreEmphMix",
  "highShelfGainDb", "highShelfFcHz", "highShelfQ",
  "voicedTiltDbPerOct", "noiseGlottalModDepth",
  "pitchSyncF1DeltaHz", "pitchSyncB1DeltaHz",
  "speedQuotient", "aspirationTiltDbPerOct", "cascadeBwScale", "tremorDepth",
  "nasalBwScale", "f4FreqScale", "nasalGainScale",
  "chorusDepth", "chorusDetuneHz",
};

// A built-in voice at neutral sliders: the DSP defaults, with the formant
// sharpness NVDA's neutral slider gives (1.0).  No chorus: depth 0, and the
// 2 Hz detune the engine uses when a profile does not say.
static const double kToneNeutral[kProfileToneKeyCount] = {
  0.91, 0.92, 0.35, 4.0, 2000.0, 0.7,
  0.0, 0.0, 0.0, 0.0,
  2.0, 0.0, 1.0, 0.0,
  1.0, 1.0, 1.0,
  0.0, 2.0,
};

double profileToneNeutral(int i) {
  return (i >= 0 && i < kProfileToneKeyCount) ? kToneNeutral[i] : 0.0;
}

double profileToneFallback(int i, bool profileHasToneBlock) {
  // nvspFrontend_getVoicingTone fills an explicit block's missing high
  // shelf with 5.5 dB; everything else matches the neutral values.
  if (i == 3 && profileHasToneBlock) return 5.5;
  return profileToneNeutral(i);
}

bool parseScaleStrict(const std::string& text, double& out) {
  const std::string s = trim(text);
  if (s.empty()) return false;
  try {
    size_t pos = 0;
    const double v = std::stod(s, &pos);
    if (pos != s.size()) return false;
    if (!std::isfinite(v) || v < 0.0 || v > 3.0) return false;
    out = v;
    return true;
  } catch (...) {
    return false;
  }
}

// The pitch and formant shape of a built-in voice as class scales on the two
// root classes (every phoneme is a vowel or a consonant, and the frontend
// applies each matching class cumulatively, so nothing is put on the narrower
// classes).  Mirrors applySpeechSettingsToFrame; absolute values there have
// no multiplier form and are reported in outNote instead.
static void seedClassScalesFromPreset(const std::string& voice, VPVoiceProfile& p, std::string& outNote) {
  VPClassScales s;
  auto arr = [](std::array<double, 6>& a, std::array<bool, 6>& f, std::initializer_list<double> v) {
    size_t i = 0;
    for (double x : v) { if (i < 6) { a[i] = x; f[i] = true; } ++i; }
  };
  auto num = [](double& d, bool& f, double v) { d = v; f = true; };
  if (voice == "Adam") {
    arr(s.cb_mul, s.cb_mul_set, {1.3, 1, 1, 1, 1, 1});
    arr(s.pa_mul, s.pa_mul_set, {1, 1, 1, 1, 1, 1.3});
    num(s.fricationAmplitude_mul, s.fricationAmplitude_mul_set, 0.85);
  } else if (voice == "David") {
    num(s.voicePitch_mul, s.voicePitch_mul_set, 0.75);
    num(s.endVoicePitch_mul, s.endVoicePitch_mul_set, 0.75);
    arr(s.cf_mul, s.cf_mul_set, {0.90, 0.93, 0.95, 1, 1, 1});
  } else if (voice == "Benjamin") {
    arr(s.cf_mul, s.cf_mul_set, {1.01, 1.02, 1, 1, 1, 1});
    arr(s.cb_mul, s.cb_mul_set, {1.3, 1, 1, 1, 1, 1});
    arr(s.pa_mul, s.pa_mul_set, {1, 1, 1, 1, 1, 1.3});
    num(s.fricationAmplitude_mul, s.fricationAmplitude_mul_set, 0.7);
    outNote = "Benjamin's fixed upper formants (cf4 3770, cf5 4100, cf6 5000 Hz) and its nasal pole shift are absolute values with no class-scale form; they were not carried over.";
  } else if (voice == "Caleb") {
    num(s.voiceAmplitude_mul, s.voiceAmplitude_mul_set, 0.0);
    outNote = "Caleb's full aspiration is an absolute value with no class-scale form; the whisper here comes from voiceAmplitude_mul 0 alone.";
  } else if (voice == "Robert") {
    num(s.voicePitch_mul, s.voicePitch_mul_set, 1.10);
    num(s.endVoicePitch_mul, s.endVoicePitch_mul_set, 1.10);
    arr(s.cf_mul, s.cf_mul_set, {1.02, 1.06, 1.08, 1.08, 1.10, 1.05});
    arr(s.cb_mul, s.cb_mul_set, {0.65, 0.68, 0.72, 0.75, 0.78, 0.80});
    arr(s.pf_mul, s.pf_mul_set, {1, 1, 1.06, 1.08, 1.10, 1.00});
    arr(s.pb_mul, s.pb_mul_set, {0.72, 0.75, 0.78, 0.80, 0.82, 0.85});
    arr(s.pa_mul, s.pa_mul_set, {1, 1, 1.08, 1.15, 1.20, 1.25});
    num(s.voiceTurbulenceAmplitude_mul, s.voiceTurbulenceAmplitude_mul_set, 0.20);
    num(s.fricationAmplitude_mul, s.fricationAmplitude_mul_set, 0.75);
    num(s.vibratoPitchOffset_mul, s.vibratoPitchOffset_mul_set, 0.0);
    num(s.vibratoSpeed_mul, s.vibratoSpeed_mul_set, 0.0);
    outNote = "Robert's pressed glottis (glottalOpenQuotient 0.30, an absolute value) and its parallel bypass scale have no class-scale form; they were not carried over.";
  } else {
    return;
  }
  p.classScales["vowel"] = s;
  p.classScales["consonant"] = s;
}


static std::string formatToneValue(double v) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(6) << v;
  std::string s = oss.str();
  const size_t dot = s.find('.');
  if (dot != std::string::npos) {
    const size_t last = s.find_last_not_of('0');
    if (last != std::string::npos && last >= dot) s = s.substr(0, last + 1);  // "4.000000" -> "4."
    if (!s.empty() && s.back() == '.') s.pop_back();                           // "4." -> "4"
  }
  if (s == "-0") s = "0";
  return s;
}

void applyProfileSave(std::vector<VPVoiceProfile>& profiles, const ProfileSaveRequest& req, std::string& outNote) {
  outNote.clear();
  auto find = [&](const std::string& n) -> VPVoiceProfile* {
    for (auto& p : profiles) if (p.name == n) return &p;
    return nullptr;
  };
  VPVoiceProfile* target = find(req.name);
  if (!target) {
    VPVoiceProfile fresh;
    const VPVoiceProfile* src = req.sourceProfile.empty() ? nullptr : find(req.sourceProfile);
    if (src) {
      fresh = *src;                       // class scales, overrides, voice source
    } else if (!req.baseVoice.empty()) {
      seedClassScalesFromPreset(req.baseVoice, fresh, outNote);
    }
    fresh.name = req.name;
    profiles.push_back(std::move(fresh));
    target = &profiles.back();
  }
  // The source's stored text is kept for sliders that did not move, so an
  // untouched value is not rounded to a slider step.  That only holds when
  // the target carries the source's values: itself, or a fresh copy of it.
  const bool keepUnmoved = !req.sourceProfile.empty() &&
      (req.name == req.sourceProfile || find(req.sourceProfile) != nullptr);
  // The high shelf this profile plays before the save.  A missing key means
  // 5.5 dB inside a voicingTone block and 4 dB without one, so adding or
  // emptying a block changes an absent shelf's meaning; it is kept below.
  double shelfBefore = profileToneFallback(3, !target->voicingTone.empty());
  {
    auto it = target->voicingTone.find("highShelfGainDb");
    if (it != target->voicingTone.end()) {
      double v;
      if (parseDouble(it->second, v) && std::isfinite(v)) shelfBefore = v;
    }
  }
  const double shelfWanted = (keepUnmoved && !req.toneMoved[3]) ? shelfBefore : req.tone[3];
  for (int i = 0; i < kProfileToneKeyCount; ++i) {
    const std::string key = kProfileToneKeys[i];
    if (keepUnmoved && !req.toneMoved[i]) continue;
    if (std::fabs(req.tone[i] - kToneNeutral[i]) < 1e-9) {
      target->voicingTone.erase(key);
    } else {
      target->voicingTone[key] = formatToneValue(req.tone[i]);
    }
  }
  // With no shelf key the profile plays 5.5 dB if it has a block and 4 dB if
  // not.  When that is not the shelf wanted (the moved slider, or what it
  // played before the save), write the shelf explicitly.
  if (target->voicingTone.find("highShelfGainDb") == target->voicingTone.end()) {
    const double implied = target->voicingTone.empty() ? 4.0 : 5.5;
    if (std::fabs(shelfWanted - implied) > 1e-9)
      target->voicingTone["highShelfGainDb"] = formatToneValue(shelfWanted);
  }
  target->hasVoicingTone = !target->voicingTone.empty();
  // The typed scale is the profile's own; 1 means "as the listener set it".
  target->inflectionScale = req.inflectionScale;
  target->hasInflectionScale = std::fabs(req.inflectionScale - 1.0) > 1e-9;
}

void profileToneToSliders(const VPVoiceProfile& profile, std::vector<int>& voicingSliders,
                          std::vector<double>* outValues) {
  const VPVoiceProfile* prof = &profile;
  if (voicingSliders.size() < static_cast<size_t>(kProfileToneKeyCount))
    voicingSliders.resize(static_cast<size_t>(kProfileToneKeyCount), 50);
  const bool hasBlock = prof->hasVoicingTone && !prof->voicingTone.empty();
  for (int i = 0; i < kProfileToneKeyCount; ++i) {
    double v = profileToneFallback(i, hasBlock);
    auto it = prof->voicingTone.find(kProfileToneKeys[i]);
    if (it != prof->voicingTone.end()) {
      char* end = nullptr;
      const double parsed = std::strtod(it->second.c_str(), &end);
      if (end != it->second.c_str() && std::isfinite(parsed)) v = parsed;
    }
    voicingSliders[static_cast<size_t>(i)] = mapVoicingValueToSlider(i, v);
    if (outValues) {
      if (outValues->size() < static_cast<size_t>(kProfileToneKeyCount))
        outValues->resize(static_cast<size_t>(kProfileToneKeyCount), 0.0);
      (*outValues)[static_cast<size_t>(i)] = v;
    }
  }
}

void slidersToSaveRequest(const std::vector<int>& voicingSliders, const std::string& sourceProfile,
                          const std::vector<int>& sourceSliders, ProfileSaveRequest& req) {
  const bool haveBaseline = !sourceProfile.empty() &&
      sourceSliders.size() >= static_cast<size_t>(kProfileToneKeyCount);
  for (int i = 0; i < kProfileToneKeyCount; ++i) {
    const int s = (static_cast<size_t>(i) < voicingSliders.size()) ? voicingSliders[static_cast<size_t>(i)] : 50;
    // A slider at its neutral position stands for the neutral value itself.
    // For every slider but chorus detune that is what the map gives anyway;
    // detune rests at 33, which the map makes 1.985 Hz, not the engine's 2,
    // and an untouched save would write `chorusDetuneHz: 1.985`.
    req.tone[i] = (s == voicingSliderNeutral(i)) ? kToneNeutral[i] : mapVoicingSliderToValue(i, s);
    req.toneMoved[i] = !haveBaseline || s != sourceSliders[static_cast<size_t>(i)];
  }
}

} // namespace tgsb_editor
