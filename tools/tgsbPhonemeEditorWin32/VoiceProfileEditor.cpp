/*
TGSpeechBox — Voice profile editor UI.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#define UNICODE
#define _UNICODE

#include "VoiceProfileEditor.h"
#include "WinUtils.h"
#include "resource.h"

#include <commctrl.h>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <regex>
#include <iomanip>
#include <cmath>
#include <initializer_list>

namespace tgsb_editor {

// =============================================================================
// YAML Parsing helpers
// =============================================================================

static std::string trim(const std::string& s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

static int countIndent(const std::string& line) {
  int n = 0;
  for (char c : line) {
    if (c == ' ') n++;
    else if (c == '\t') n += 2;
    else break;
  }
  return n;
}

// Alias for compatibility
static int getIndentLevel(const std::string& line) {
  return countIndent(line);
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

static std::vector<double> parseDoubleArray(const std::string& s) {
  std::vector<double> result;
  std::string inner = s;
  
  // Remove brackets
  size_t start = inner.find('[');
  size_t end = inner.rfind(']');
  if (start != std::string::npos && end != std::string::npos && end > start) {
    inner = inner.substr(start + 1, end - start - 1);
  }
  
  // Split by comma
  std::stringstream ss(inner);
  std::string token;
  while (std::getline(ss, token, ',')) {
    double v;
    if (parseDouble(trim(token), v)) {
      result.push_back(v);
    }
  }
  return result;
}

// Store one class-scale field into the typed members.  Returns false when the
// field is not one the editor knows (the caller may keep it elsewhere).
static bool setScaleField(VPClassScales& scales, const std::string& field, const std::string& value) {
  // Array fields
  if (field == "cf_mul") {
    auto arr = parseDoubleArray(value);
    for (size_t i = 0; i < arr.size() && i < 6; i++) {
      scales.cf_mul[i] = arr[i];
      scales.cf_mul_set[i] = true;
    }
    return true;
  }
  if (field == "pf_mul") {
    auto arr = parseDoubleArray(value);
    for (size_t i = 0; i < arr.size() && i < 6; i++) {
      scales.pf_mul[i] = arr[i];
      scales.pf_mul_set[i] = true;
    }
    return true;
  }
  if (field == "cb_mul") {
    auto arr = parseDoubleArray(value);
    for (size_t i = 0; i < arr.size() && i < 6; i++) {
      scales.cb_mul[i] = arr[i];
      scales.cb_mul_set[i] = true;
    }
    return true;
  }
  if (field == "pb_mul") {
    auto arr = parseDoubleArray(value);
    for (size_t i = 0; i < arr.size() && i < 6; i++) {
      scales.pb_mul[i] = arr[i];
      scales.pb_mul_set[i] = true;
    }
    return true;
  }
  if (field == "pa_mul") {
    auto arr = parseDoubleArray(value);
    for (size_t i = 0; i < arr.size() && i < 6; i++) {
      scales.pa_mul[i] = arr[i];
      scales.pa_mul_set[i] = true;
    }
    return true;
  }
  
  // Scalar fields
  double v;
  if (!parseDouble(value, v)) return false;
  
  if (field == "voicePitch_mul") { scales.voicePitch_mul = v; scales.voicePitch_mul_set = true; }
  else if (field == "endVoicePitch_mul") { scales.endVoicePitch_mul = v; scales.endVoicePitch_mul_set = true; }
  else if (field == "vibratoPitchOffset_mul") { scales.vibratoPitchOffset_mul = v; scales.vibratoPitchOffset_mul_set = true; }
  else if (field == "vibratoSpeed_mul") { scales.vibratoSpeed_mul = v; scales.vibratoSpeed_mul_set = true; }
  else if (field == "voiceTurbulenceAmplitude_mul") { scales.voiceTurbulenceAmplitude_mul = v; scales.voiceTurbulenceAmplitude_mul_set = true; }
  else if (field == "glottalOpenQuotient_mul") { scales.glottalOpenQuotient_mul = v; scales.glottalOpenQuotient_mul_set = true; }
  else if (field == "voiceAmplitude_mul") { scales.voiceAmplitude_mul = v; scales.voiceAmplitude_mul_set = true; }
  else if (field == "aspirationAmplitude_mul") { scales.aspirationAmplitude_mul = v; scales.aspirationAmplitude_mul_set = true; }
  else if (field == "fricationAmplitude_mul") { scales.fricationAmplitude_mul = v; scales.fricationAmplitude_mul_set = true; }
  else if (field == "preFormantGain_mul") { scales.preFormantGain_mul = v; scales.preFormantGain_mul_set = true; }
  else if (field == "outputGain_mul") { scales.outputGain_mul = v; scales.outputGain_mul_set = true; }
  else return false;
  return true;
}

// Parse inline map like {cf1: 648, cf2: 1856, cf3: 2820}
static std::map<std::string, double> parseInlineMap(const std::string& s) {
  std::map<std::string, double> result;
  std::string inner = s;
  
  size_t start = inner.find('{');
  size_t end = inner.rfind('}');
  if (start != std::string::npos && end != std::string::npos && end > start) {
    inner = inner.substr(start + 1, end - start - 1);
  }
  
  // Split by comma, then by colon
  std::stringstream ss(inner);
  std::string token;
  while (std::getline(ss, token, ',')) {
    size_t colon = token.find(':');
    if (colon != std::string::npos) {
      std::string key = trim(token.substr(0, colon));
      std::string val = trim(token.substr(colon + 1));
      double v;
      if (parseDouble(val, v)) {
        result[key] = v;
      }
    }
  }
  return result;
}

bool loadVoiceProfilesFromYaml(const std::wstring& yamlPath,
                           std::vector<VPVoiceProfile>& outProfiles,
                           std::string& outError) {
  outProfiles.clear();
  outError.clear();

  std::ifstream f(yamlPath);
  if (!f.is_open()) {
    outError = "Failed to open phonemes.yaml for reading.";
    return false;
  }

  std::string line;
  bool inVoiceProfiles = false;
  int voiceProfilesIndent = -1;

  VPVoiceProfile* currentProfile = nullptr;
  VPPhonemeOverride* currentOverride = nullptr;

  int profileIndent = -1;

  // classScales state
  int classScalesIndent = -1;
  int classIndent = -1;
  int fieldIndent = -1;
  bool inClassScales = false;
  std::string currentClass;

  // phonemeOverrides state
  int overridesIndent = -1;
  int overrideIndent = -1;
  bool inPhonemeOverrides = false;

  // voicingTone state (optional)
  int voicingToneIndent = -1;
  int voicingToneFieldIndent = -1;
  bool inVoicingTone = false;

  while (std::getline(f, line)) {
    std::string trimmedLine = trim(line);
    if (trimmedLine.empty() || trimmedLine[0] == '#') {
      continue;
    }

    if (!inVoiceProfiles) {
      if (trimmedLine == "voiceProfiles:") {
        inVoiceProfiles = true;
        voiceProfilesIndent = getIndentLevel(line);
      }
      continue;
    }

    int indent = getIndentLevel(line);
    std::string stripped = trimmedLine;

    // Exit voiceProfiles section if we hit another key at same or lower indent.
    if (indent <= voiceProfilesIndent && stripped.back() == ':' &&
        stripped != "voiceProfiles:") {
      inVoiceProfiles = false;
      break;
    }

    if (profileIndent < 0) {
      profileIndent = indent;
    }

    // New profile header line.
    if (indent == profileIndent) {
      auto pos = stripped.find(':');
      if (pos != std::string::npos) {
        std::string profileName = trim(stripped.substr(0, pos));

        outProfiles.emplace_back();
        outProfiles.back().name = profileName;
        currentProfile = &outProfiles.back();
        currentOverride = nullptr;

        // Reset all section state.
        classScalesIndent = -1;
        classIndent = -1;
        fieldIndent = -1;
        inClassScales = false;
        currentClass.clear();

        overridesIndent = -1;
        overrideIndent = -1;
        inPhonemeOverrides = false;

        voicingToneIndent = -1;
        voicingToneFieldIndent = -1;
        inVoicingTone = false;
      }
      continue;
    }

    if (!currentProfile) {
      continue;
    }

    // If indentation stepped back to (or above) a section header, we left it.
    if (inClassScales && indent <= classScalesIndent) {
      inClassScales = false;
      classScalesIndent = -1;
      classIndent = -1;
      fieldIndent = -1;
      currentClass.clear();
    }
    if (inPhonemeOverrides && indent <= overridesIndent) {
      inPhonemeOverrides = false;
      overridesIndent = -1;
      overrideIndent = -1;
      currentOverride = nullptr;
    }
    if (inVoicingTone && indent <= voicingToneIndent) {
      inVoicingTone = false;
      voicingToneIndent = -1;
      voicingToneFieldIndent = -1;
    }

    // Section headers at first level under the profile.
    if (indent > profileIndent) {
      if (stripped.rfind("inflectionScale:", 0) == 0) {
        double v;
        if (parseDouble(trim(stripped.substr(std::string("inflectionScale:").size())), v)) {
          currentProfile->inflectionScale = v;
          currentProfile->hasInflectionScale = true;
        }
        continue;
      }
      if (stripped == "classScales:") {
        inClassScales = true;
        classScalesIndent = indent;
        classIndent = -1;
        fieldIndent = -1;
        currentClass.clear();

        inPhonemeOverrides = false;
        inVoicingTone = false;
        continue;
      }

      if (stripped == "phonemeOverrides:") {
        inPhonemeOverrides = true;
        overridesIndent = indent;
        overrideIndent = -1;
        currentOverride = nullptr;

        inClassScales = false;
        inVoicingTone = false;
        continue;
      }

      if (stripped.rfind("voicingTone:", 0) == 0) {
        currentProfile->hasVoicingTone = true;

        inVoicingTone = true;
        voicingToneIndent = indent;
        voicingToneFieldIndent = -1;

        inClassScales = false;
        inPhonemeOverrides = false;

        // Inline map form: voicingTone: {a: 1.0, b: 2.0}
        auto colonPos = stripped.find(':');
        std::string rest =
            (colonPos != std::string::npos) ? trim(stripped.substr(colonPos + 1))
                                            : std::string();
        if (!rest.empty() && rest[0] == '{') {
          auto kvs = parseInlineMap(rest);
          for (const auto& kv : kvs) {
            // Convert double to string for storage
            std::ostringstream oss;
            oss << kv.second;
            currentProfile->voicingTone[kv.first] = oss.str();
          }

          // No nested lines expected for inline form.
          inVoicingTone = false;
          voicingToneIndent = -1;
          voicingToneFieldIndent = -1;
        }
        continue;
      }
    }

    // Parse classScales section.
    if (inClassScales && indent > classScalesIndent) {
      if (classIndent < 0) {
        classIndent = indent;
      }

      if (indent == classIndent) {
        // Class name line: "vowels:" etc
        auto pos = stripped.find(':');
        if (pos != std::string::npos) {
          currentClass = trim(stripped.substr(0, pos));
          currentProfile->classScales[currentClass] = VPClassScales();
          fieldIndent = -1;
        }
      } else if (!currentClass.empty()) {
        if (fieldIndent < 0) {
          fieldIndent = indent;
        }

        if (indent == fieldIndent) {
          auto pos = stripped.find(':');
          if (pos != std::string::npos) {
            std::string field = trim(stripped.substr(0, pos));
            std::string valueStr = trim(stripped.substr(pos + 1));
            // Known fields (the scalars and the six-element arrays) go into
            // the typed members the dialog shows and the writer emits; any
            // other numeric field is kept in the generic map so a Save &
            // Close preserves it.  (Before this, every field went into the
            // map, arrays failed to parse and were dropped, and the dialog
            // showed loaded profiles as empty.)
            if (!setScaleField(currentProfile->classScales[currentClass], field, valueStr)) {
              double value;
              if (parseDouble(valueStr, value)) {
                currentProfile->classScales[currentClass].scales[field] = value;
              }
            }
          }
        }
      }
      continue;
    }

    // Parse phonemeOverrides section (inline map form only, like before).
    if (inPhonemeOverrides && indent > overridesIndent) {
      if (overrideIndent < 0) {
        overrideIndent = indent;
      }

      if (indent == overrideIndent) {
        auto pos = stripped.find(':');
        if (pos != std::string::npos) {
          std::string overrideName = trim(stripped.substr(0, pos));
          std::string mapPart = trim(stripped.substr(pos + 1));

          VPPhonemeOverride newOverride;
          newOverride.phoneme = overrideName;
          
          auto parsedMap = parseInlineMap(mapPart);
          for (const auto& kv : parsedMap) {
            // kv.second is already a double from parseInlineMap
            newOverride.fields[kv.first] = kv.second;
          }
          
          currentProfile->phonemeOverrides.push_back(newOverride);
          currentOverride = &currentProfile->phonemeOverrides.back();
        }
      }
      continue;
    }

    // Parse voicingTone section (simple key: value pairs).
    if (inVoicingTone && indent > voicingToneIndent) {
      if (voicingToneFieldIndent < 0) {
        voicingToneFieldIndent = indent;
      }

      if (indent == voicingToneFieldIndent) {
        auto pos = stripped.find(':');
        if (pos != std::string::npos) {
          std::string field = trim(stripped.substr(0, pos));
          std::string value = trim(stripped.substr(pos + 1));
          if (!field.empty()) {
            currentProfile->voicingTone[field] = value;
          }
        }
      }
      continue;
    }
  }

  return true;
}


// Format a double array as YAML [1.0, 1.1, 1.2]
static std::string formatArray(const std::array<double, 6>& arr, const std::array<bool, 6>& set) {
  // Check if any are set
  bool anySet = false;
  for (bool b : set) if (b) { anySet = true; break; }
  if (!anySet) return "";
  
  std::ostringstream ss;
  ss << "[";
  for (size_t i = 0; i < 6; i++) {
    if (i > 0) ss << ", ";
    ss << arr[i];
  }
  ss << "]";
  return ss.str();
}

static std::string formatDouble(double v) {
  std::ostringstream ss;
  ss << v;
  return ss.str();
}

// ---------------------------------------------------------------------------
// Voice source of a profile (see VoiceProfileEditor.h)

const char* const kProfileToneKeys[kProfileToneKeyCount] = {
  "voicingPeakPos", "voicedPreEmphA", "voicedPreEmphMix",
  "highShelfGainDb", "highShelfFcHz", "highShelfQ",
  "voicedTiltDbPerOct", "noiseGlottalModDepth",
  "pitchSyncF1DeltaHz", "pitchSyncB1DeltaHz",
  "speedQuotient", "aspirationTiltDbPerOct", "cascadeBwScale", "tremorDepth",
  "nasalBwScale", "f4FreqScale", "nasalGainScale",
};

// A built-in voice at neutral sliders: the DSP defaults, with the formant
// sharpness NVDA's neutral slider gives (1.0).
static const double kToneNeutral[kProfileToneKeyCount] = {
  0.91, 0.92, 0.35, 4.0, 2000.0, 0.7,
  0.0, 0.0, 0.0, 0.0,
  2.0, 0.0, 1.0, 0.0,
  1.0, 1.0, 1.0,
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
    arr(s.pf_mul, s.pf_mul_set, {1, 1, 1.06, 1.08, 1.10, 1.05});
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
  for (int i = 0; i < kProfileToneKeyCount; ++i) {
    const std::string key = kProfileToneKeys[i];
    if (keepUnmoved && !req.toneMoved[i]) continue;
    if (std::fabs(req.tone[i] - kToneNeutral[i]) < 1e-9) {
      target->voicingTone.erase(key);
    } else {
      target->voicingTone[key] = formatToneValue(req.tone[i]);
    }
  }
  // A block without a high shelf plays 5.5 dB (the frontend's fallback), not
  // the 4 dB a built-in voice plays.  When the shelf slider says otherwise,
  // pin it so the profile sounds as previewed.
  // (Only when the shelf is the saver's to set: an unmoved shelf on a loaded
  // profile already plays what the preview played.)
  if ((!keepUnmoved || req.toneMoved[3]) &&
      !target->voicingTone.empty() &&
      target->voicingTone.find("highShelfGainDb") == target->voicingTone.end() &&
      std::fabs(req.tone[3] - 5.5) > 1e-9) {
    target->voicingTone["highShelfGainDb"] = formatToneValue(req.tone[3]);
  }
  target->hasVoicingTone = !target->voicingTone.empty();
  // The typed scale is the profile's own; 1 means "as the listener set it".
  target->inflectionScale = req.inflectionScale;
  target->hasInflectionScale = std::fabs(req.inflectionScale - 1.0) > 1e-9;
}

// Emit one class's scale fields: the typed scalars and arrays (what the dialog
// edits), then any unknown numeric fields kept in the generic map.
static void appendClassScaleLines(std::vector<std::string>& out, const VPClassScales& s, int indent) {
  const std::string pad(static_cast<size_t>(indent), ' ');
  auto scalar = [&](const char* name, double v, bool set) {
    if (!set) return;
    std::ostringstream ss;
    ss << pad << name << ": " << v;
    out.push_back(ss.str());
  };
  auto array = [&](const char* name, const std::array<double, 6>& arr, const std::array<bool, 6>& set) {
    std::string a = formatArray(arr, set);
    if (a.empty()) return;
    out.push_back(pad + name + ": " + a);
  };
  scalar("voicePitch_mul", s.voicePitch_mul, s.voicePitch_mul_set);
  scalar("endVoicePitch_mul", s.endVoicePitch_mul, s.endVoicePitch_mul_set);
  scalar("vibratoPitchOffset_mul", s.vibratoPitchOffset_mul, s.vibratoPitchOffset_mul_set);
  scalar("vibratoSpeed_mul", s.vibratoSpeed_mul, s.vibratoSpeed_mul_set);
  scalar("voiceTurbulenceAmplitude_mul", s.voiceTurbulenceAmplitude_mul, s.voiceTurbulenceAmplitude_mul_set);
  scalar("glottalOpenQuotient_mul", s.glottalOpenQuotient_mul, s.glottalOpenQuotient_mul_set);
  scalar("voiceAmplitude_mul", s.voiceAmplitude_mul, s.voiceAmplitude_mul_set);
  scalar("aspirationAmplitude_mul", s.aspirationAmplitude_mul, s.aspirationAmplitude_mul_set);
  scalar("fricationAmplitude_mul", s.fricationAmplitude_mul, s.fricationAmplitude_mul_set);
  scalar("preFormantGain_mul", s.preFormantGain_mul, s.preFormantGain_mul_set);
  scalar("outputGain_mul", s.outputGain_mul, s.outputGain_mul_set);
  array("cf_mul", s.cf_mul, s.cf_mul_set);
  array("pf_mul", s.pf_mul, s.pf_mul_set);
  array("cb_mul", s.cb_mul, s.cb_mul_set);
  array("pb_mul", s.pb_mul, s.pb_mul_set);
  array("pa_mul", s.pa_mul, s.pa_mul_set);
  for (const auto& kv : s.scales) {
    std::ostringstream ss;
    ss << pad << kv.first << ": " << kv.second;
    out.push_back(ss.str());
  }
}

bool saveVoiceProfilesToYaml(const std::wstring& yamlPath,
                           const std::vector<VPVoiceProfile>& profiles,
                           std::string& outError) {
  outError.clear();

  // Read original file into memory.
  std::ifstream inFile(yamlPath);
  if (!inFile.is_open()) {
    outError = "Failed to open phonemes.yaml for reading.";
    return false;
  }

  std::vector<std::string> originalLines;
  std::string line;
  while (std::getline(inFile, line)) {
    originalLines.push_back(line);
  }
  inFile.close();

  // Locate the voiceProfiles: block.
  int vpStart = -1;
  int vpIndent = -1;
  int vpEnd = (int)originalLines.size();

  for (int i = 0; i < (int)originalLines.size(); ++i) {
    std::string t = trim(originalLines[i]);
    if (t == "voiceProfiles:") {
      vpStart = i;
      vpIndent = getIndentLevel(originalLines[i]);
      break;
    }
  }

  if (vpStart < 0) {
    outError = "phonemes.yaml does not contain a voiceProfiles: section.";
    return false;
  }

  for (int i = vpStart + 1; i < (int)originalLines.size(); ++i) {
    std::string t = trim(originalLines[i]);
    if (t.empty() || t[0] == '#') {
      continue;
    }

    int indent = getIndentLevel(originalLines[i]);
    if (indent <= vpIndent && t.back() == ':' && t != "voiceProfiles:") {
      vpEnd = i;
      break;
    }
  }

  // Build new voiceProfiles: block.
  std::vector<std::string> newVP;
  newVP.push_back(std::string(vpIndent, ' ') + "voiceProfiles:");

  for (const auto& profile : profiles) {
    newVP.push_back(std::string(vpIndent + 2, ' ') + profile.name + ":");
    if (profile.hasInflectionScale) {
      std::ostringstream ss;
      ss << std::string(vpIndent + 4, ' ') << "inflectionScale: " << profile.inflectionScale;
      newVP.push_back(ss.str());
    }

    // voicingTone section (optional). Always write it back if it existed, so the
    // editor doesn't destroy manual edits.
    if (profile.hasVoicingTone || !profile.voicingTone.empty()) {
      if (profile.voicingTone.empty()) {
        newVP.push_back(std::string(vpIndent + 4, ' ') + "voicingTone:");
      } else {
        std::ostringstream ss;
        ss << std::string(vpIndent + 4, ' ') << "voicingTone: {";
        bool first = true;
        for (const auto& kv : profile.voicingTone) {
          if (!first)
            ss << ", ";
          first = false;
          ss << kv.first << ": " << kv.second;
        }
        ss << "}";
        newVP.push_back(ss.str());
      }
    }

    // classScales
    if (!profile.classScales.empty()) {
      newVP.push_back(std::string(vpIndent + 4, ' ') + "classScales:");
      for (const auto& cls : profile.classScales) {
        newVP.push_back(std::string(vpIndent + 6, ' ') + cls.first + ":");
        appendClassScaleLines(newVP, cls.second, vpIndent + 8);
      }
    }

    // phonemeOverrides (inline map form, like before)
    if (!profile.phonemeOverrides.empty()) {
      newVP.push_back(std::string(vpIndent + 4, ' ') + "phonemeOverrides:");
      for (const auto& ov : profile.phonemeOverrides) {
        std::ostringstream ss;
        ss << std::string(vpIndent + 6, ' ') << ov.phoneme << ": {";
        bool first = true;
        for (const auto& kv : ov.fields) {
          if (!first)
            ss << ", ";
          first = false;
          ss << kv.first << ": " << kv.second;
        }
        ss << "}";
        newVP.push_back(ss.str());
      }
    }
  }

  // Assemble final output lines.
  std::vector<std::string> outLines;
  outLines.insert(outLines.end(), originalLines.begin(),
                  originalLines.begin() + vpStart);
  outLines.insert(outLines.end(), newVP.begin(), newVP.end());
  outLines.insert(outLines.end(), originalLines.begin() + vpEnd,
                  originalLines.end());

  // Write file back.
  std::ofstream outFile(yamlPath, std::ios::trunc);
  if (!outFile.is_open()) {
    outError = "Failed to open phonemes.yaml for writing.";
    return false;
  }

  for (size_t i = 0; i < outLines.size(); ++i) {
    outFile << outLines[i];
    if (i + 1 < outLines.size())
      outFile << "\n";
  }

  outFile.close();
  return true;
}


// =============================================================================
// Dialog Procedures
// =============================================================================

static void populateProfileList(HWND hList, const std::vector<VPVoiceProfile>& profiles) {
  SendMessageW(hList, LB_RESETCONTENT, 0, 0);
  for (const auto& p : profiles) {
    std::wstring w = utf8ToWide(p.name);
    SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
  }
  if (!profiles.empty()) {
    SendMessageW(hList, LB_SETCURSEL, 0, 0);
  }
}

static INT_PTR CALLBACK VoiceProfilesDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  auto* st = reinterpret_cast<VoiceProfilesDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
  
  switch (msg) {
    case WM_INITDIALOG: {
      st = reinterpret_cast<VoiceProfilesDialogState*>(lParam);
      SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
      
      HWND hList = GetDlgItem(hDlg, IDC_VP_LIST);
      populateProfileList(hList, st->profiles);
      
      return TRUE;
    }
    
    case WM_COMMAND: {
      if (!st) break;
      int id = LOWORD(wParam);
      int code = HIWORD(wParam);
      
      if (id == IDC_VP_ADD) {
        EditVoiceProfileDialogState eps;
        eps.profile.name = "NewVoice";
        if (ShowEditVoiceProfileDialog(GetModuleHandleW(nullptr), hDlg, eps) && eps.ok) {
          st->profiles.push_back(eps.profile);
          st->modified = true;
          populateProfileList(GetDlgItem(hDlg, IDC_VP_LIST), st->profiles);
          SendMessageW(GetDlgItem(hDlg, IDC_VP_LIST), LB_SETCURSEL, st->profiles.size() - 1, 0);
        }
        return TRUE;
      }
      
      if (id == IDC_VP_EDIT || (id == IDC_VP_LIST && code == LBN_DBLCLK)) {
        HWND hList = GetDlgItem(hDlg, IDC_VP_LIST);
        int sel = static_cast<int>(SendMessageW(hList, LB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel < static_cast<int>(st->profiles.size())) {
          EditVoiceProfileDialogState eps;
          eps.profile = st->profiles[sel];
          if (ShowEditVoiceProfileDialog(GetModuleHandleW(nullptr), hDlg, eps) && eps.ok) {
            st->profiles[sel] = eps.profile;
            st->modified = true;
            populateProfileList(hList, st->profiles);
            SendMessageW(hList, LB_SETCURSEL, sel, 0);
          }
        }
        return TRUE;
      }
      
      if (id == IDC_VP_DELETE) {
        HWND hList = GetDlgItem(hDlg, IDC_VP_LIST);
        int sel = static_cast<int>(SendMessageW(hList, LB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel < static_cast<int>(st->profiles.size())) {
          std::wstring msg = L"Delete voice profile \"" + utf8ToWide(st->profiles[sel].name) + L"\"?";
          if (MessageBoxW(hDlg, msg.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            st->profiles.erase(st->profiles.begin() + sel);
            st->modified = true;
            populateProfileList(hList, st->profiles);
          }
        }
        return TRUE;
      }
      
      if (id == IDC_VP_DUPLICATE) {
        HWND hList = GetDlgItem(hDlg, IDC_VP_LIST);
        int sel = static_cast<int>(SendMessageW(hList, LB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel < static_cast<int>(st->profiles.size())) {
          VPVoiceProfile dup = st->profiles[sel];
          dup.name += "_copy";
          st->profiles.push_back(dup);
          st->modified = true;
          populateProfileList(hList, st->profiles);
          SendMessageW(hList, LB_SETCURSEL, st->profiles.size() - 1, 0);
        }
        return TRUE;
      }
      
      if (id == IDOK) {
        st->ok = true;
        EndDialog(hDlg, IDOK);
        return TRUE;
      }
      
      if (id == IDCANCEL) {
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
      }
      break;
    }
  }
  return FALSE;
}

// =============================================================================
// Edit Voice Profile Dialog
// =============================================================================

static void populateClassCombo(HWND hCombo, const std::string& selected) {
  SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
  int sel = 0;
  for (int i = 0; i < kVoiceProfileClassCount; i++) {
    std::wstring w = utf8ToWide(kVoiceProfileClasses[i]);
    int idx = static_cast<int>(SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str())));
    if (kVoiceProfileClasses[i] == selected) sel = idx;
  }
  SendMessageW(hCombo, CB_SETCURSEL, sel, 0);
}

static void populateScalesList(HWND hList, const VPClassScales& scales) {
  SendMessageW(hList, LB_RESETCONTENT, 0, 0);
  
  auto addScalar = [&](const char* name, double val, bool set) {
    if (set) {
      std::wstring w = utf8ToWide(name) + L": " + utf8ToWide(formatDouble(val));
      SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
    }
  };
  
  addScalar("voicePitch_mul", scales.voicePitch_mul, scales.voicePitch_mul_set);
  addScalar("endVoicePitch_mul", scales.endVoicePitch_mul, scales.endVoicePitch_mul_set);
  addScalar("vibratoPitchOffset_mul", scales.vibratoPitchOffset_mul, scales.vibratoPitchOffset_mul_set);
  addScalar("vibratoSpeed_mul", scales.vibratoSpeed_mul, scales.vibratoSpeed_mul_set);
  addScalar("voiceTurbulenceAmplitude_mul", scales.voiceTurbulenceAmplitude_mul, scales.voiceTurbulenceAmplitude_mul_set);
  addScalar("glottalOpenQuotient_mul", scales.glottalOpenQuotient_mul, scales.glottalOpenQuotient_mul_set);
  addScalar("voiceAmplitude_mul", scales.voiceAmplitude_mul, scales.voiceAmplitude_mul_set);
  addScalar("aspirationAmplitude_mul", scales.aspirationAmplitude_mul, scales.aspirationAmplitude_mul_set);
  addScalar("fricationAmplitude_mul", scales.fricationAmplitude_mul, scales.fricationAmplitude_mul_set);
  addScalar("preFormantGain_mul", scales.preFormantGain_mul, scales.preFormantGain_mul_set);
  addScalar("outputGain_mul", scales.outputGain_mul, scales.outputGain_mul_set);
  
  // Arrays
  auto addArray = [&](const char* name, const std::array<double, 6>& arr, const std::array<bool, 6>& set) {
    std::string s = formatArray(arr, set);
    if (!s.empty()) {
      std::wstring w = utf8ToWide(name) + L": " + utf8ToWide(s);
      SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
    }
  };
  
  addArray("cf_mul", scales.cf_mul, scales.cf_mul_set);
  addArray("pf_mul", scales.pf_mul, scales.pf_mul_set);
  addArray("cb_mul", scales.cb_mul, scales.cb_mul_set);
  addArray("pb_mul", scales.pb_mul, scales.pb_mul_set);
  addArray("pa_mul", scales.pa_mul, scales.pa_mul_set);
}

static void populateFieldCombo(HWND hCombo) {
  SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
  for (int i = 0; i < kScaleFieldCount; i++) {
    std::wstring w = utf8ToWide(kScaleFieldNames[i]);
    SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
  }
  SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
}

static void populateOverridesList(HWND hList, const std::vector<VPPhonemeOverride>& overrides) {
  SendMessageW(hList, LB_RESETCONTENT, 0, 0);
  for (const auto& ovr : overrides) {
    std::wstring w = utf8ToWide(ovr.phoneme) + L" (" + std::to_wstring(ovr.fields.size()) + L" fields)";
    SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
  }
}

static INT_PTR CALLBACK EditVoiceProfileDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  auto* st = reinterpret_cast<EditVoiceProfileDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
  
  switch (msg) {
    case WM_INITDIALOG: {
      st = reinterpret_cast<EditVoiceProfileDialogState*>(lParam);
      SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
      
      // Profile name
      SetDlgItemTextW(hDlg, IDC_EVP_NAME, utf8ToWide(st->profile.name).c_str());
      // Inflection scale (1 = the listener's own setting)
      SetDlgItemTextW(hDlg, IDC_EVP_INFLECTION,
                      st->profile.hasInflectionScale ? utf8ToWide(formatDouble(st->profile.inflectionScale)).c_str() : L"1");
      
      // Class combo
      HWND hClassCombo = GetDlgItem(hDlg, IDC_EVP_CLASS_COMBO);
      populateClassCombo(hClassCombo, st->currentClass.empty() ? "vowel" : st->currentClass);
      if (st->currentClass.empty()) st->currentClass = "vowel";
      
      // Field combo
      populateFieldCombo(GetDlgItem(hDlg, IDC_EVP_SCALE_FIELD));
      
      // Scales list for current class
      auto it = st->profile.classScales.find(st->currentClass);
      if (it != st->profile.classScales.end()) {
        populateScalesList(GetDlgItem(hDlg, IDC_EVP_SCALES_LIST), it->second);
      }
      
      // Overrides list
      populateOverridesList(GetDlgItem(hDlg, IDC_EVP_OVERRIDES_LIST), st->profile.phonemeOverrides);
      
      return TRUE;
    }
    
    case WM_COMMAND: {
      if (!st) break;
      int id = LOWORD(wParam);
      int code = HIWORD(wParam);
      
      // Class combo changed
      if (id == IDC_EVP_CLASS_COMBO && code == CBN_SELCHANGE) {
        HWND hCombo = GetDlgItem(hDlg, IDC_EVP_CLASS_COMBO);
        int sel = static_cast<int>(SendMessageW(hCombo, CB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel < kVoiceProfileClassCount) {
          st->currentClass = kVoiceProfileClasses[sel];
          auto it = st->profile.classScales.find(st->currentClass);
          if (it != st->profile.classScales.end()) {
            populateScalesList(GetDlgItem(hDlg, IDC_EVP_SCALES_LIST), it->second);
          } else {
            SendMessageW(GetDlgItem(hDlg, IDC_EVP_SCALES_LIST), LB_RESETCONTENT, 0, 0);
          }
        }
        return TRUE;
      }
      
      // Remove class
      if (id == IDC_EVP_CLASS_REMOVE) {
        if (!st->currentClass.empty()) {
          auto it = st->profile.classScales.find(st->currentClass);
          if (it != st->profile.classScales.end()) {
            st->profile.classScales.erase(it);
            SendMessageW(GetDlgItem(hDlg, IDC_EVP_SCALES_LIST), LB_RESETCONTENT, 0, 0);
            std::wstring msg = L"All fields from class \"" + utf8ToWide(st->currentClass) + L"\" removed.";
            msgBox(hDlg, msg.c_str(), L"Class Removed", MB_OK | MB_ICONINFORMATION);
          } else {
            msgBox(hDlg, L"This class has no fields to remove.", L"Class Empty", MB_OK | MB_ICONINFORMATION);
          }
        }
        return TRUE;
      }
      
      // Set scale value (auto-creates class if needed)
      if (id == IDC_EVP_SCALE_SET) {
        if (st->currentClass.empty()) {
          msgBox(hDlg, L"Select a class first.", L"Voice Profile", MB_ICONINFORMATION);
          return TRUE;
        }
        
        // Get field name
        HWND hFieldCombo = GetDlgItem(hDlg, IDC_EVP_SCALE_FIELD);
        int fieldSel = static_cast<int>(SendMessageW(hFieldCombo, CB_GETCURSEL, 0, 0));
        if (fieldSel < 0 || fieldSel >= kScaleFieldCount) return TRUE;
        std::string fieldName = kScaleFieldNames[fieldSel];
        
        // Get value
        wchar_t buf[256];
        GetDlgItemTextW(hDlg, IDC_EVP_SCALE_VALUE, buf, 256);
        std::string valueStr = wideToUtf8(buf);
        
        // Ensure class exists
        if (st->profile.classScales.find(st->currentClass) == st->profile.classScales.end()) {
          st->profile.classScales[st->currentClass] = VPClassScales{};
        }
        
        setScaleField(st->profile.classScales[st->currentClass], fieldName, valueStr);
        populateScalesList(GetDlgItem(hDlg, IDC_EVP_SCALES_LIST), st->profile.classScales[st->currentClass]);
        return TRUE;
      }
      
      // Add phoneme override
      if (id == IDC_EVP_OVERRIDE_ADD) {
        EditPhonemeOverrideDialogState ops;
        if (ShowEditPhonemeOverrideDialog(GetModuleHandleW(nullptr), hDlg, ops) && ops.ok) {
          st->profile.phonemeOverrides.push_back(ops.override);
          populateOverridesList(GetDlgItem(hDlg, IDC_EVP_OVERRIDES_LIST), st->profile.phonemeOverrides);
        }
        return TRUE;
      }
      
      // Edit phoneme override
      if (id == IDC_EVP_OVERRIDE_EDIT || (id == IDC_EVP_OVERRIDES_LIST && code == LBN_DBLCLK)) {
        HWND hList = GetDlgItem(hDlg, IDC_EVP_OVERRIDES_LIST);
        int sel = static_cast<int>(SendMessageW(hList, LB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel < static_cast<int>(st->profile.phonemeOverrides.size())) {
          EditPhonemeOverrideDialogState ops;
          ops.override = st->profile.phonemeOverrides[sel];
          if (ShowEditPhonemeOverrideDialog(GetModuleHandleW(nullptr), hDlg, ops) && ops.ok) {
            st->profile.phonemeOverrides[sel] = ops.override;
            populateOverridesList(hList, st->profile.phonemeOverrides);
          }
        }
        return TRUE;
      }
      
      // Remove phoneme override
      if (id == IDC_EVP_OVERRIDE_REMOVE) {
        HWND hList = GetDlgItem(hDlg, IDC_EVP_OVERRIDES_LIST);
        int sel = static_cast<int>(SendMessageW(hList, LB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel < static_cast<int>(st->profile.phonemeOverrides.size())) {
          st->profile.phonemeOverrides.erase(st->profile.phonemeOverrides.begin() + sel);
          populateOverridesList(hList, st->profile.phonemeOverrides);
        }
        return TRUE;
      }
      
      if (id == IDOK) {
        // Get profile name
        wchar_t buf[256];
        GetDlgItemTextW(hDlg, IDC_EVP_NAME, buf, 256);
        st->profile.name = wideToUtf8(buf);
        
        if (st->profile.name.empty()) {
          msgBox(hDlg, L"Profile name is required.", L"Voice Profile", MB_ICONERROR);
          return TRUE;
        }
        // Inflection scale: empty or 1 means "not set".
        {
          wchar_t ibuf[64];
          GetDlgItemTextW(hDlg, IDC_EVP_INFLECTION, ibuf, 64);
          std::string inflStr = trim(wideToUtf8(ibuf));
          double infl = 1.0;
          if (!inflStr.empty() && !parseScaleStrict(inflStr, infl)) {
            msgBox(hDlg, L"Inflection scale must be a number from 0 to 3 (1 = unchanged, 1.3 = a third livelier).", L"Voice Profile", MB_ICONERROR);
            return TRUE;
          }
          st->profile.inflectionScale = infl;
          st->profile.hasInflectionScale = (infl < 0.999999 || infl > 1.000001);
        }
        
        st->ok = true;
        EndDialog(hDlg, IDOK);
        return TRUE;
      }
      
      if (id == IDCANCEL) {
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
      }
      break;
    }
  }
  return FALSE;
}

// =============================================================================
// Edit Phoneme Override Dialog
// =============================================================================

static void populateOverrideFieldCombo(HWND hCombo) {
  SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
  for (int i = 0; i < kOverrideFieldCount; i++) {
    std::wstring w = utf8ToWide(kOverrideFieldNames[i]);
    SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
  }
  SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
}

static void populateOverrideFieldsList(HWND hList, const std::map<std::string, double>& fields) {
  SendMessageW(hList, LB_RESETCONTENT, 0, 0);
  for (const auto& [name, val] : fields) {
    std::wstring w = utf8ToWide(name) + L": " + utf8ToWide(formatDouble(val));
    SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
  }
}

static INT_PTR CALLBACK EditPhonemeOverrideDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  auto* st = reinterpret_cast<EditPhonemeOverrideDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
  
  switch (msg) {
    case WM_INITDIALOG: {
      st = reinterpret_cast<EditPhonemeOverrideDialogState*>(lParam);
      SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
      
      SetDlgItemTextW(hDlg, IDC_EPO_PHONEME, utf8ToWide(st->override.phoneme).c_str());
      populateOverrideFieldCombo(GetDlgItem(hDlg, IDC_EPO_FIELD_COMBO));
      populateOverrideFieldsList(GetDlgItem(hDlg, IDC_EPO_FIELDS_LIST), st->override.fields);
      
      return TRUE;
    }
    
    case WM_COMMAND: {
      if (!st) break;
      int id = LOWORD(wParam);
      
      // Set field
      if (id == IDC_EPO_FIELD_SET) {
        HWND hCombo = GetDlgItem(hDlg, IDC_EPO_FIELD_COMBO);
        int sel = static_cast<int>(SendMessageW(hCombo, CB_GETCURSEL, 0, 0));
        if (sel < 0 || sel >= kOverrideFieldCount) return TRUE;
        
        wchar_t buf[256];
        GetDlgItemTextW(hDlg, IDC_EPO_FIELD_VALUE, buf, 256);
        double val;
        if (parseDouble(wideToUtf8(buf), val)) {
          st->override.fields[kOverrideFieldNames[sel]] = val;
          populateOverrideFieldsList(GetDlgItem(hDlg, IDC_EPO_FIELDS_LIST), st->override.fields);
        } else {
          msgBox(hDlg, L"Invalid number.", L"Override Field", MB_ICONERROR);
        }
        return TRUE;
      }
      
      // Remove field
      if (id == IDC_EPO_FIELD_REMOVE) {
        HWND hList = GetDlgItem(hDlg, IDC_EPO_FIELDS_LIST);
        int sel = static_cast<int>(SendMessageW(hList, LB_GETCURSEL, 0, 0));
        if (sel >= 0) {
          // Find which field this is
          int idx = 0;
          for (auto it = st->override.fields.begin(); it != st->override.fields.end(); ++it, ++idx) {
            if (idx == sel) {
              st->override.fields.erase(it);
              break;
            }
          }
          populateOverrideFieldsList(hList, st->override.fields);
        }
        return TRUE;
      }
      
      if (id == IDOK) {
        wchar_t buf[256];
        GetDlgItemTextW(hDlg, IDC_EPO_PHONEME, buf, 256);
        st->override.phoneme = wideToUtf8(buf);
        
        if (st->override.phoneme.empty()) {
          msgBox(hDlg, L"Phoneme is required.", L"Override", MB_ICONERROR);
          return TRUE;
        }
        
        st->ok = true;
        EndDialog(hDlg, IDOK);
        return TRUE;
      }
      
      if (id == IDCANCEL) {
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
      }
      break;
    }
  }
  return FALSE;
}

// =============================================================================
// Public entry points
// =============================================================================

bool ShowVoiceProfilesDialog(HINSTANCE hInst, HWND parent, VoiceProfilesDialogState& st) {
  st.ok = false;
  DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_VOICE_PROFILES), parent, VoiceProfilesDlgProc, reinterpret_cast<LPARAM>(&st));
  return st.ok;
}

bool ShowEditVoiceProfileDialog(HINSTANCE hInst, HWND parent, EditVoiceProfileDialogState& st) {
  st.ok = false;
  DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_EDIT_VOICE_PROFILE), parent, EditVoiceProfileDlgProc, reinterpret_cast<LPARAM>(&st));
  return st.ok;
}

bool ShowEditPhonemeOverrideDialog(HINSTANCE hInst, HWND parent, EditPhonemeOverrideDialogState& st) {
  st.ok = false;
  DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_EDIT_PHONEME_OVERRIDE), parent, EditPhonemeOverrideDlgProc, reinterpret_cast<LPARAM>(&st));
  return st.ok;
}

} // namespace tgsb_editor