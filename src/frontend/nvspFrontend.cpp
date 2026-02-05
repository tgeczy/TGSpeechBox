#include "nvspFrontend.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <new>
#include <set>
#include <string>
#include <vector>

#include "ipa_engine.h"
#include "pack.h"

namespace nvsp_frontend {

struct Handle {
  std::string packDir;
  PackSet pack;
  bool packLoaded = false;
  // True once we have emitted at least one chunk of speech on this handle.
  // Used to optionally insert a tiny silence between consecutive queueIPA calls.
  bool streamHasSpeech = false;
  // True if the last emitted *real phoneme* in the previous chunk was vowel-like
  // (vowel or semivowel). Used to avoid inserting boundary pauses inside
  // vowel-to-vowel transitions (e.g. diphthongs split across chunks).
  bool lastEndsVowelLike = false;
  std::string langTag;
  std::string lastError;
  std::mutex mu;
  
  // Per-handle trajectory limiting state for formant smoothing.
  // This is NOT static - each handle has its own state to avoid data races
  // when multiple engine instances speak concurrently.
  TrajectoryState trajectoryState;
  
  // User-level FrameEx defaults (ABI v2+).
  // These are mixed with per-phoneme values when emitting frames.
  double frameExCreakiness = 0.0;
  double frameExBreathiness = 0.0;
  double frameExJitter = 0.0;
  double frameExShimmer = 0.0;
  double frameExSharpness = 1.0;  // multiplier, 1.0 = neutral
  
  // Buffer for getVoiceProfileNames return value
  std::string profileNamesBuffer;
};

static Handle* asHandle(nvspFrontend_handle_t h) {
  return reinterpret_cast<Handle*>(h);
}

static void setError(Handle* h, const std::string& msg) {
  if (!h) return;
  h->lastError = msg;
}

// Helper to format a double with minimal precision (avoid "2.000000")
// Defined here (outside extern "C") to avoid C4190 warning about std::string.
static std::string formatDouble(double val, int precision = 2) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%.*f", precision, val);
  // Trim trailing zeros after decimal point
  std::string s = buf;
  if (s.find('.') != std::string::npos) {
    size_t lastNonZero = s.find_last_not_of('0');
    if (lastNonZero != std::string::npos && s[lastNonZero] == '.') {
      // Keep at least one decimal (e.g., "2.0" not "2.")
      s = s.substr(0, lastNonZero + 2);
    } else if (lastNonZero != std::string::npos) {
      s = s.substr(0, lastNonZero + 1);
    }
  }
  return s;
}

} // namespace nvsp_frontend

extern "C" {

NVSP_FRONTEND_API nvspFrontend_handle_t nvspFrontend_create(const char* packDirUtf8) {
  using namespace nvsp_frontend;
  try {
    auto* h = new Handle();
    h->packDir = packDirUtf8 ? std::string(packDirUtf8) : std::string();
    h->lastError.clear();
    return reinterpret_cast<nvspFrontend_handle_t>(h);
  } catch (...) {
    return nullptr;
  }
}

NVSP_FRONTEND_API void nvspFrontend_destroy(nvspFrontend_handle_t handle) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  delete h;
}

NVSP_FRONTEND_API int nvspFrontend_setLanguage(nvspFrontend_handle_t handle, const char* langTagUtf8) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h) return 0;

  std::lock_guard<std::mutex> lock(h->mu);

  h->lastError.clear();
  const std::string lang = langTagUtf8 ? std::string(langTagUtf8) : std::string();

  PackSet pack;
  std::string err;
  if (!loadPackSet(h->packDir, lang, pack, err)) {
    setError(h, err.empty() ? "Failed to load pack set" : err);
    return 0;
  }

  h->pack = std::move(pack);
  h->packLoaded = true;
  // Treat language change as the start of a new stream, so we don't
  // insert a segment boundary gap before the first chunk in the new language.
  h->streamHasSpeech = false;
  h->lastEndsVowelLike = false;
  h->langTag = normalizeLangTag(lang);
  return 1;
}

NVSP_FRONTEND_API int nvspFrontend_queueIPA(
  nvspFrontend_handle_t handle,
  const char* ipaUtf8,
  double speed,
  double basePitch,
  double inflection,
  const char* clauseTypeUtf8,
  int userIndexBase,
  nvspFrontend_FrameCallback cb,
  void* userData
) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h) return 0;

  std::lock_guard<std::mutex> lock(h->mu);
  h->lastError.clear();

  if (!h->packLoaded) {
    // Default to "default" language if the caller didn't call setLanguage.
    PackSet pack;
    std::string err;
    if (!loadPackSet(h->packDir, "default", pack, err)) {
      setError(h, err.empty() ? "No language loaded and default load failed" : err);
      return 0;
    }
    h->pack = std::move(pack);
    h->packLoaded = true;
    h->langTag = "default";
  }

  if (!ipaUtf8) ipaUtf8 = "";

  char clauseType = '.';
  if (clauseTypeUtf8 && clauseTypeUtf8[0]) {
    clauseType = clauseTypeUtf8[0];
  }

  std::vector<Token> tokens;
  std::string err;
  if (!convertIpaToTokens(h->pack, ipaUtf8, speed, basePitch, inflection, clauseType, tokens, err)) {
    setError(h, err.empty() ? "IPA conversion failed" : err);
    return 0;
  }

  // Determine whether this chunk starts/ends with a vowel-like phoneme.
  // We ignore silence/preStopGap tokens for this purpose.
  const Token* firstReal = nullptr;
  const Token* lastReal = nullptr;
  for (const Token& t : tokens) {
    if (!t.def || t.silence) continue;
    if (!firstReal) firstReal = &t;
    lastReal = &t;
  }

  auto isVowelLike = [](const Token& t) -> bool {
    if (!t.def) return false;
    const std::uint32_t f = t.def->flags;
    return (f & kIsVowel) || (f & kIsSemivowel);
  };

  auto isLiquidLike = [](const Token& t) -> bool {
    if (!t.def) return false;
    const std::uint32_t f = t.def->flags;
    return (f & kIsLiquid) || (f & kIsTap) || (f & kIsTrill);
  };

  const bool startsVowelLike = firstReal && isVowelLike(*firstReal);
  const bool startsLiquidLike = firstReal && isLiquidLike(*firstReal);
  const bool endsVowelLike = lastReal && isVowelLike(*lastReal);
  const bool hasRealPhoneme = (firstReal != nullptr);

  // Optional: insert a short silence between consecutive queueIPA calls.
  // This helps when callers stitch UI speech from multiple chunks.
  //
  // However, a boundary pause can create an audible "hole" in vowel-to-vowel
  // transitions (e.g. when a diphthong is split across chunks). To keep
  // diphthongs smooth while preserving consonant clarity, we suppress the
  // boundary gap when the previous chunk ended with a vowel/semivowel and
  // the next chunk starts with a vowel/semivowel.
  if (cb && h->streamHasSpeech && hasRealPhoneme) {
    const double gapMs = h->pack.lang.segmentBoundaryGapMs;
    const double fadeMs = h->pack.lang.segmentBoundaryFadeMs;
    if (gapMs > 0.0 || fadeMs > 0.0) {
      bool skip = false;
      if (h->pack.lang.segmentBoundarySkipVowelToVowel &&
          h->lastEndsVowelLike && startsVowelLike) {
        skip = true;
      }
      if (!skip && h->pack.lang.segmentBoundarySkipVowelToLiquid &&
          h->lastEndsVowelLike && startsLiquidLike) {
        skip = true;
      }
      if (!skip) {
        const double spd = (speed > 0.0) ? speed : 1.0;
        cb(userData, nullptr, gapMs / spd, fadeMs / spd, userIndexBase);
      }
    }
  }

  emitFrames(h->pack, tokens, userIndexBase, &h->trajectoryState, cb, userData);
  if (hasRealPhoneme) {
    h->streamHasSpeech = true;
    h->lastEndsVowelLike = endsVowelLike;
  }
  return 1;
}

NVSP_FRONTEND_API int nvspFrontend_setVoiceProfile(nvspFrontend_handle_t handle, const char* profileNameUtf8) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h) return 0;

  std::lock_guard<std::mutex> lock(h->mu);
  h->lastError.clear();

  // Set the voice profile name in the language pack settings.
  // This will be used during the next queueIPA call.
  h->pack.lang.voiceProfileName = profileNameUtf8 ? std::string(profileNameUtf8) : std::string();
  return 1;
}

NVSP_FRONTEND_API const char* nvspFrontend_getVoiceProfile(nvspFrontend_handle_t handle) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h) return "";

  std::lock_guard<std::mutex> lock(h->mu);
  return h->pack.lang.voiceProfileName.c_str();
}

NVSP_FRONTEND_API const char* nvspFrontend_getPackWarnings(nvspFrontend_handle_t handle) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h) return "";

  std::lock_guard<std::mutex> lock(h->mu);
  return h->pack.loadWarnings.c_str();
}

NVSP_FRONTEND_API const char* nvspFrontend_getLastError(nvspFrontend_handle_t handle) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h) return "invalid handle";
  std::lock_guard<std::mutex> lock(h->mu);
  return h->lastError.c_str();
}

NVSP_FRONTEND_API int nvspFrontend_getABIVersion(void) {
  return NVSP_FRONTEND_ABI_VERSION;
}

NVSP_FRONTEND_API void nvspFrontend_setFrameExDefaults(
  nvspFrontend_handle_t handle,
  double creakiness,
  double breathiness,
  double jitter,
  double shimmer,
  double sharpness
) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h) return;

  std::lock_guard<std::mutex> lock(h->mu);
  h->frameExCreakiness = creakiness;
  h->frameExBreathiness = breathiness;
  h->frameExJitter = jitter;
  h->frameExShimmer = shimmer;
  h->frameExSharpness = sharpness;
}

NVSP_FRONTEND_API int nvspFrontend_getFrameExDefaults(
  nvspFrontend_handle_t handle,
  nvspFrontend_FrameEx* outDefaults
) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h || !outDefaults) return 0;

  std::lock_guard<std::mutex> lock(h->mu);
  outDefaults->creakiness = h->frameExCreakiness;
  outDefaults->breathiness = h->frameExBreathiness;
  outDefaults->jitter = h->frameExJitter;
  outDefaults->shimmer = h->frameExShimmer;
  outDefaults->sharpness = h->frameExSharpness;
  // Formant end targets: NAN means "no ramping" - per-phoneme only
  outDefaults->endCf1 = NAN;
  outDefaults->endCf2 = NAN;
  outDefaults->endCf3 = NAN;
  outDefaults->endPf1 = NAN;
  outDefaults->endPf2 = NAN;
  outDefaults->endPf3 = NAN;
  return 1;
}

NVSP_FRONTEND_API int nvspFrontend_queueIPA_Ex(
  nvspFrontend_handle_t handle,
  const char* ipaUtf8,
  double speed,
  double basePitch,
  double inflection,
  const char* clauseTypeUtf8,
  int userIndexBase,
  nvspFrontend_FrameExCallback cb,
  void* userData
) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h) return 0;

  std::lock_guard<std::mutex> lock(h->mu);
  h->lastError.clear();

  if (!h->packLoaded) {
    // Default to "default" language if the caller didn't call setLanguage.
    PackSet pack;
    std::string err;
    if (!loadPackSet(h->packDir, "default", pack, err)) {
      setError(h, err.empty() ? "No language loaded and default load failed" : err);
      return 0;
    }
    h->pack = std::move(pack);
    h->packLoaded = true;
    h->langTag = "default";
  }

  if (!ipaUtf8) ipaUtf8 = "";

  char clauseType = '.';
  if (clauseTypeUtf8 && clauseTypeUtf8[0]) {
    clauseType = clauseTypeUtf8[0];
  }

  std::vector<Token> tokens;
  std::string err;
  if (!convertIpaToTokens(h->pack, ipaUtf8, speed, basePitch, inflection, clauseType, tokens, err)) {
    setError(h, err.empty() ? "IPA conversion failed" : err);
    return 0;
  }

  // Determine whether this chunk starts/ends with a vowel-like phoneme.
  const Token* firstReal = nullptr;
  const Token* lastReal = nullptr;
  for (const Token& t : tokens) {
    if (!t.def || t.silence) continue;
    if (!firstReal) firstReal = &t;
    lastReal = &t;
  }

  auto isVowelLike = [](const Token& t) -> bool {
    if (!t.def) return false;
    const std::uint32_t f = t.def->flags;
    return (f & kIsVowel) || (f & kIsSemivowel);
  };

  auto isLiquidLike = [](const Token& t) -> bool {
    if (!t.def) return false;
    const std::uint32_t f = t.def->flags;
    return (f & kIsLiquid) || (f & kIsTap) || (f & kIsTrill);
  };

  const bool startsVowelLike = firstReal && isVowelLike(*firstReal);
  const bool startsLiquidLike = firstReal && isLiquidLike(*firstReal);
  const bool endsVowelLike = lastReal && isVowelLike(*lastReal);
  const bool hasRealPhoneme = (firstReal != nullptr);

  // Optional: insert a short silence between consecutive queueIPA calls.
  if (cb && h->streamHasSpeech && hasRealPhoneme) {
    const double gapMs = h->pack.lang.segmentBoundaryGapMs;
    const double fadeMs = h->pack.lang.segmentBoundaryFadeMs;
    if (gapMs > 0.0 || fadeMs > 0.0) {
      bool skip = false;
      if (h->pack.lang.segmentBoundarySkipVowelToVowel &&
          h->lastEndsVowelLike && startsVowelLike) {
        skip = true;
      }
      if (!skip && h->pack.lang.segmentBoundarySkipVowelToLiquid &&
          h->lastEndsVowelLike && startsLiquidLike) {
        skip = true;
      }
      if (!skip) {
        const double spd = (speed > 0.0) ? speed : 1.0;
        cb(userData, nullptr, nullptr, gapMs / spd, fadeMs / spd, userIndexBase);
      }
    }
  }

  // Build FrameEx defaults struct to pass to emitFramesEx
  nvspFrontend_FrameEx frameExDefaults;
  frameExDefaults.creakiness = h->frameExCreakiness;
  frameExDefaults.breathiness = h->frameExBreathiness;
  frameExDefaults.jitter = h->frameExJitter;
  frameExDefaults.shimmer = h->frameExShimmer;
  frameExDefaults.sharpness = h->frameExSharpness;
  // Formant end targets: NAN means "no ramping" - per-phoneme only, no user defaults
  frameExDefaults.endCf1 = NAN;
  frameExDefaults.endCf2 = NAN;
  frameExDefaults.endCf3 = NAN;
  frameExDefaults.endPf1 = NAN;
  frameExDefaults.endPf2 = NAN;
  frameExDefaults.endPf3 = NAN;

  emitFramesEx(h->pack, tokens, userIndexBase, frameExDefaults, &h->trajectoryState, cb, userData);
  
  if (hasRealPhoneme) {
    h->streamHasSpeech = true;
    h->lastEndsVowelLike = endsVowelLike;
  }
  return 1;
}

NVSP_FRONTEND_API int nvspFrontend_getVoicingTone(
  nvspFrontend_handle_t handle,
  nvspFrontend_VoicingTone* outTone
) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h || !outTone) return 0;

  std::lock_guard<std::mutex> lock(h->mu);

  // Initialize with defaults
  outTone->voicingPeakPos = 0.0;
  outTone->voicedPreEmphA = 0.0;
  outTone->voicedPreEmphMix = 0.0;
  outTone->highShelfGainDb = 0.0;
  outTone->highShelfFcHz = 0.0;
  outTone->highShelfQ = 0.0;
  outTone->voicedTiltDbPerOct = 0.0;
  outTone->noiseGlottalModDepth = 0.0;
  outTone->pitchSyncF1DeltaHz = 0.0;
  outTone->pitchSyncB1DeltaHz = 0.0;
  outTone->speedQuotient = 2.0;  // neutral default
  outTone->aspirationTiltDbPerOct = 0.0;
  outTone->cascadeBwScale = 1.0; // neutral default
  outTone->tremorDepth = 0.0;    // no tremor by default

  // Check if we have a voice profile with voicing tone
  const std::string& profileName = h->pack.lang.voiceProfileName;
  if (profileName.empty()) return 0;

  if (!h->pack.voiceProfiles) return 0;
  const VoiceProfile* profile = h->pack.voiceProfiles->getProfile(profileName);
  if (!profile) return 0;
  if (!profile->hasVoicingTone) return 0;

  // Copy the voicing tone values
  const VoicingTone& vt = profile->voicingTone;
  if (vt.voicingPeakPos_set) outTone->voicingPeakPos = vt.voicingPeakPos;
  if (vt.voicedPreEmphA_set) outTone->voicedPreEmphA = vt.voicedPreEmphA;
  if (vt.voicedPreEmphMix_set) outTone->voicedPreEmphMix = vt.voicedPreEmphMix;
  if (vt.highShelfGainDb_set) outTone->highShelfGainDb = vt.highShelfGainDb;
  if (vt.highShelfFcHz_set) outTone->highShelfFcHz = vt.highShelfFcHz;
  if (vt.highShelfQ_set) outTone->highShelfQ = vt.highShelfQ;
  if (vt.voicedTiltDbPerOct_set) outTone->voicedTiltDbPerOct = vt.voicedTiltDbPerOct;
  if (vt.noiseGlottalModDepth_set) outTone->noiseGlottalModDepth = vt.noiseGlottalModDepth;
  if (vt.pitchSyncF1DeltaHz_set) outTone->pitchSyncF1DeltaHz = vt.pitchSyncF1DeltaHz;
  if (vt.pitchSyncB1DeltaHz_set) outTone->pitchSyncB1DeltaHz = vt.pitchSyncB1DeltaHz;
  if (vt.speedQuotient_set) outTone->speedQuotient = vt.speedQuotient;
  if (vt.aspirationTiltDbPerOct_set) outTone->aspirationTiltDbPerOct = vt.aspirationTiltDbPerOct;
  if (vt.cascadeBwScale_set) outTone->cascadeBwScale = vt.cascadeBwScale;
  if (vt.tremorDepth_set) outTone->tremorDepth = vt.tremorDepth;

  return 1;  // Profile has explicit voicing tone
}

NVSP_FRONTEND_API const char* nvspFrontend_getVoiceProfileNames(nvspFrontend_handle_t handle) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h) return "";

  std::lock_guard<std::mutex> lock(h->mu);

  // Build newline-separated list of profile names
  h->profileNamesBuffer.clear();
  
  if (h->pack.voiceProfiles) {
    for (const auto& kv : h->pack.voiceProfiles->profiles) {
      h->profileNamesBuffer += kv.first;
      h->profileNamesBuffer += '\n';
    }
  }

  return h->profileNamesBuffer.c_str();
}

NVSP_FRONTEND_API int nvspFrontend_saveVoiceProfileSliders(
  nvspFrontend_handle_t handle,
  const char* profileNameUtf8,
  const nvspFrontend_VoiceProfileSliders* sliders
) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h || !profileNameUtf8 || !sliders) {
    if (h) setError(h, "Invalid parameters");
    return 0;
  }

  std::lock_guard<std::mutex> lock(h->mu);

  std::string profileName = profileNameUtf8;
  if (profileName.empty()) {
    setError(h, "Profile name cannot be empty");
    return 0;
  }

  // Build path to phonemes.yaml
  std::string phonemesPath = h->packDir + "/phonemes.yaml";
  
  // Read current file content
  std::ifstream inFile(phonemesPath);
  if (!inFile.is_open()) {
    setError(h, "Cannot open phonemes.yaml for reading");
    return 0;
  }
  
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(inFile, line)) {
    // Remove trailing \r if present (Windows line endings)
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  inFile.close();

  // The 12 slider keys we write (7 VoicingTone + 5 FrameEx)
  // Order matters for nice YAML output
  struct SliderDef {
    const char* key;
    double value;
    int precision;
  };
  std::vector<SliderDef> sliderDefs = {
    {"voicedTiltDbPerOct", sliders->voicedTiltDbPerOct, 2},
    {"noiseGlottalModDepth", sliders->noiseGlottalModDepth, 2},
    {"pitchSyncF1DeltaHz", sliders->pitchSyncF1DeltaHz, 1},
    {"pitchSyncB1DeltaHz", sliders->pitchSyncB1DeltaHz, 1},
    {"speedQuotient", sliders->speedQuotient, 2},
    {"aspirationTiltDbPerOct", sliders->aspirationTiltDbPerOct, 2},
    {"cascadeBwScale", sliders->cascadeBwScale, 2},
    {"tremorDepth", sliders->tremorDepth, 2},
    {"creakiness", sliders->creakiness, 2},
    {"breathiness", sliders->breathiness, 2},
    {"jitter", sliders->jitter, 2},
    {"shimmer", sliders->shimmer, 2},
    {"sharpness", sliders->sharpness, 2},
  };

  // Track which slider keys we've written (to avoid duplicates)
  std::set<std::string> writtenKeys;

  // State machine for parsing
  bool inVoiceProfiles = false;
  bool inTargetProfile = false;
  bool inVoicingTone = false;
  bool foundProfile = false;
  bool foundVoicingTone = false;
  int voiceProfilesIndent = -1;
  int profileIndent = -1;
  int voicingToneIndent = -1;
  int voicingToneContentIndent = -1;

  std::vector<std::string> newLines;

  auto getIndent = [](const std::string& s) -> int {
    int indent = 0;
    for (char c : s) {
      if (c == ' ') indent++;
      else break;
    }
    return indent;
  };

  auto makeIndent = [](int n) -> std::string {
    return std::string(static_cast<size_t>(n), ' ');
  };

  // Helper to write all slider values
  auto writeAllSliders = [&](int indent) {
    for (const auto& sd : sliderDefs) {
      if (writtenKeys.find(sd.key) == writtenKeys.end()) {
        newLines.push_back(makeIndent(indent) + sd.key + ": " + formatDouble(sd.value, sd.precision));
        writtenKeys.insert(sd.key);
      }
    }
  };

  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string& curLine = lines[i];
    bool skipLine = false;  // Set true to skip adding curLine to output
    
    std::string stripped = curLine;
    // Trim leading/trailing whitespace for comparison
    size_t start = stripped.find_first_not_of(" \t");
    if (start != std::string::npos) {
      stripped = stripped.substr(start);
    } else {
      stripped = "";
    }
    
    int indent = getIndent(curLine);

    // Check for voiceProfiles: at root level
    if (!curLine.empty() && curLine[0] != ' ' && curLine[0] != '\t') {
      if (stripped.find("voiceProfiles:") == 0) {
        inVoiceProfiles = true;
        voiceProfilesIndent = 0;
        profileIndent = -1;
        inTargetProfile = false;
        inVoicingTone = false;
      } else if (inVoiceProfiles) {
        // Left voiceProfiles section - if we were in target profile, close it
        if (inTargetProfile && !foundVoicingTone) {
          // Need to add voicingTone block before leaving
          int vtIndent = (profileIndent >= 0 ? profileIndent : 2) + 2;
          newLines.push_back(makeIndent(vtIndent) + "voicingTone:");
          writeAllSliders(vtIndent + 2);
          foundVoicingTone = true;
        } else if (inVoicingTone) {
          // Write any remaining slider values
          writeAllSliders(voicingToneContentIndent >= 0 ? voicingToneContentIndent : voicingToneIndent + 2);
          inVoicingTone = false;
        }
        inVoiceProfiles = false;
        inTargetProfile = false;
      }
    }

    if (inVoiceProfiles) {
      // Detect profile indent level
      if (profileIndent < 0 && !stripped.empty() && stripped.back() == ':' && indent > voiceProfilesIndent) {
        profileIndent = indent;
      }

      // Check if this is the target profile line
      if (indent == profileIndent && !stripped.empty()) {
        std::string potentialName = stripped;
        size_t colonPos = potentialName.find(':');
        if (colonPos != std::string::npos) {
          potentialName = potentialName.substr(0, colonPos);
        }
        
        if (potentialName == profileName) {
          inTargetProfile = true;
          foundProfile = true;
          inVoicingTone = false;
          foundVoicingTone = false;
          voicingToneIndent = -1;
          writtenKeys.clear();
        } else if (inTargetProfile) {
          // Moving to a different profile
          if (!foundVoicingTone) {
            // Add voicingTone block before the new profile
            int vtIndent = profileIndent + 2;
            newLines.push_back(makeIndent(vtIndent) + "voicingTone:");
            writeAllSliders(vtIndent + 2);
            foundVoicingTone = true;
          } else if (inVoicingTone) {
            // Write remaining sliders before leaving
            writeAllSliders(voicingToneContentIndent >= 0 ? voicingToneContentIndent : voicingToneIndent + 2);
          }
          inTargetProfile = false;
          inVoicingTone = false;
        }
      }

      // Inside target profile
      if (inTargetProfile && indent > profileIndent) {
        // Check for voicingTone:
        if (stripped.find("voicingTone:") == 0 && !inVoicingTone) {
          inVoicingTone = true;
          foundVoicingTone = true;
          voicingToneIndent = indent;
          voicingToneContentIndent = -1;
          newLines.push_back(curLine);
          continue;
        }

        // Check for sibling sections (classScales, phonemeOverrides)
        if ((stripped.find("classScales:") == 0 || stripped.find("phonemeOverrides:") == 0) 
            && indent == profileIndent + 2) {
          if (inVoicingTone) {
            // Write remaining sliders before sibling section
            writeAllSliders(voicingToneContentIndent >= 0 ? voicingToneContentIndent : voicingToneIndent + 2);
            inVoicingTone = false;
          } else if (!foundVoicingTone) {
            // Add voicingTone block before sibling
            int vtIndent = profileIndent + 2;
            newLines.push_back(makeIndent(vtIndent) + "voicingTone:");
            writeAllSliders(vtIndent + 2);
            foundVoicingTone = true;
          }
        }

        // Inside voicingTone block
        if (inVoicingTone && indent > voicingToneIndent) {
          if (voicingToneContentIndent < 0) {
            voicingToneContentIndent = indent;
          }

          // Check if this line is one of our slider keys
          size_t colonPos = stripped.find(':');
          if (colonPos != std::string::npos) {
            std::string key = stripped.substr(0, colonPos);
            
            // Check if it's one of our slider keys and replace if so
            for (const auto& sd : sliderDefs) {
              if (key == sd.key) {
                // Replace with new value (skip original line)
                if (writtenKeys.find(key) == writtenKeys.end()) {
                  newLines.push_back(makeIndent(indent) + sd.key + ": " + formatDouble(sd.value, sd.precision));
                  writtenKeys.insert(key);
                }
                skipLine = true;
                break;
              }
            }
            
            // Not a slider key - preserve it (hidden params like voicingPeakPos)
            // But first check if we've left voicingTone (indent decreased)
          }
        }
        
        // Check if we've left voicingTone block (indent back to profile level or voicingTone level)
        if (inVoicingTone && indent <= voicingToneIndent) {
          // Write remaining sliders
          writeAllSliders(voicingToneContentIndent >= 0 ? voicingToneContentIndent : voicingToneIndent + 2);
          inVoicingTone = false;
        }
      }
    }

    if (!skipLine) {
      newLines.push_back(curLine);
    }
  }

  // Handle end of file cases
  if (inVoicingTone) {
    writeAllSliders(voicingToneContentIndent >= 0 ? voicingToneContentIndent : voicingToneIndent + 2);
  } else if (inTargetProfile && !foundVoicingTone) {
    int vtIndent = (profileIndent >= 0 ? profileIndent : 2) + 2;
    newLines.push_back(makeIndent(vtIndent) + "voicingTone:");
    writeAllSliders(vtIndent + 2);
  }

  // If profile wasn't found at all, add it at the end of voiceProfiles section
  if (!foundProfile) {
    // Find where voiceProfiles section ends (or end of file)
    // For simplicity, append at end of file under voiceProfiles
    bool hasVoiceProfiles = false;
    for (const auto& l : lines) {
      if (l.find("voiceProfiles:") == 0) {
        hasVoiceProfiles = true;
        break;
      }
    }
    
    if (!hasVoiceProfiles) {
      // Add voiceProfiles section
      newLines.push_back("");
      newLines.push_back("voiceProfiles:");
    }
    
    // Add the new profile
    newLines.push_back("  " + profileName + ":");
    newLines.push_back("    voicingTone:");
    for (const auto& sd : sliderDefs) {
      newLines.push_back("      " + std::string(sd.key) + ": " + formatDouble(sd.value, sd.precision));
    }
  }

  // Write back to file
  std::ofstream outFile(phonemesPath);
  if (!outFile.is_open()) {
    setError(h, "Cannot open phonemes.yaml for writing");
    return 0;
  }

  for (size_t i = 0; i < newLines.size(); ++i) {
    outFile << newLines[i];
    if (i + 1 < newLines.size()) {
      outFile << '\n';
    }
  }
  outFile.close();

  return 1;
}

} // extern "C"
