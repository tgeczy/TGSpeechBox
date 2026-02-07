/*
  TGSBRender (formerly nvspRender)
  -----------------------------------
  Small command-line helper that turns an IPA stream into audio using:
    - nvspFrontend (IPA -> formant frames)
    - speechPlayer (frames -> 16-bit PCM)

  Intended use:
    - Speech Dispatcher via sd_generic (see extras/speech-dispatcher/)

  Notes:
    - This tool reads UTF-8 from stdin and writes raw 16-bit signed
      little-endian PCM to stdout at a configurable sample rate (default 16000 Hz).
    - We deliberately keep the interface small and self-contained.
    
  DSP V5 Features:
    - VoicingTone V3 support (13 parameters)
    - FrameEx support (creakiness, breathiness, jitter, shimmer, sharpness)
    - Per-phoneme FrameEx from YAML (e.g. Danish stød creakiness) via queueIPA_Ex
    - Voice profile support via nvspFrontend_setVoiceProfile
    - --list-voices to show available profiles for speech-dispatcher config
    - Automatic voicing tone loading from YAML when --voice is specified
    
  DSP V6 Features:
    - Formant end targets for within-frame ramping (DECTalk-style transitions)
    - Fujisaki-Bartman pitch model for Eloquence-style prosody contours
    - FrameEx extended to 18 fields (144 bytes)
*/

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <iostream>
#include <string>
#include <vector>

#include "speechPlayer.h"
#include "nvspFrontend.h"

#if defined(_WIN32)
  #include <fcntl.h>
  #include <io.h>
#endif

namespace {

// ============================================================================
// VoicingTone V3 structure (must match voicingTone.h)
// ============================================================================

#ifndef SPEECHPLAYER_VOICINGTONE_MAGIC
#define SPEECHPLAYER_VOICINGTONE_MAGIC 0x32544F56u   // "VOT2"
#endif

#ifndef SPEECHPLAYER_VOICINGTONE_VERSION
#define SPEECHPLAYER_VOICINGTONE_VERSION 3u
#endif

#ifndef SPEECHPLAYER_DSP_VERSION
#define SPEECHPLAYER_DSP_VERSION 6u
#endif

struct VoicingToneV3 {
  uint32_t magic;
  uint32_t structSize;
  uint32_t structVersion;
  uint32_t dspVersion;
  // V1/V2 params
  double voicingPeakPos;
  double voicedPreEmphA;
  double voicedPreEmphMix;
  double highShelfGainDb;
  double highShelfFcHz;
  double highShelfQ;
  double voicedTiltDbPerOct;
  double noiseGlottalModDepth;
  double pitchSyncF1DeltaHz;
  double pitchSyncB1DeltaHz;
  // V3 additions
  double speedQuotient;
  double aspirationTiltDbPerOct;
  double cascadeBwScale;
  double tremorDepth;
};

// ============================================================================
// FrameEx structure (must match frame.h - 18 doubles = 144 bytes)
// ============================================================================

struct FrameEx {
  // Voice quality parameters (DSP v5)
  double creakiness;
  double breathiness;
  double jitter;
  double shimmer;
  double sharpness;
  // Formant end targets (DECTalk-style ramping)
  double endCf1;
  double endCf2;
  double endCf3;
  double endPf1;
  double endPf2;
  double endPf3;
  // Fujisaki pitch model (DSP v6+)
  double fujisakiEnabled;
  double fujisakiReset;
  double fujisakiPhraseAmp;
  double fujisakiPhraseLen;
  double fujisakiAccentAmp;
  double fujisakiAccentDur;
  double fujisakiAccentLen;
};

// ============================================================================
// Options
// ============================================================================

struct Options {
  std::string packDir = ".";      // Directory containing "packs/" or the packs folder itself.
  std::string language = "en";    // e.g. "en", "en-us", "fr".
  std::string voiceProfile = "";  // Voice profile name (empty = default)

  // Speech Dispatcher (SSIP) conventions are typically -100..+100 for rate.
  // We accept that and map it to a speed multiplier.
  int rate = 0;

  // We accept 0..100 (like eSpeak pitch after GenericPitch mapping).
  int pitch = 50;

  // We accept a linear gain multiplier. In Speech Dispatcher generic configs,
  // volume is often mapped to 0.0..2.0 (default 1.0).
  double volume = 1.0;

  // Output sample rate in Hz. Must match whatever plays the raw stream.
  int sampleRate = 16000;

  // Inflection range (octaves across +/-50% pitch path).
  double inflection = 0.5;

  // -------------------------------------------------------------------------
  // VoicingTone V3 parameters (0-100 sliders, mapped to actual values)
  // -------------------------------------------------------------------------
  int voicingPeakPos = 50;        // 0.85-0.95, default 0.91
  int voicedPreEmphA = 50;        // 0.0-0.97, default ~0.485
  int voicedPreEmphMix = 50;      // 0.0-1.0, default 0.5
  int highShelfGainDb = 50;       // -12 to +12 dB, default 0
  int highShelfFcHz = 50;         // 500-8000 Hz, default 4250
  int highShelfQ = 50;            // 0.3-2.0, default 1.15
  int voicedTiltDbPerOct = 50;    // -24 to +24, default 0
  int noiseGlottalModDepth = 0;   // 0.0-1.0, default 0
  int pitchSyncF1DeltaHz = 50;    // -60 to +60, default 0
  int pitchSyncB1DeltaHz = 50;    // -50 to +50, default 0
  int speedQuotient = 50;         // 0.5-4.0, default 2.0
  int aspirationTiltDbPerOct = 50; // -12 to +12, default 0
  int cascadeBwScale = 50;        // 0.4-1.4, default 1.0
  int tremor = 0;                 // 0.0-0.4, default 0 (no tremor)

  // -------------------------------------------------------------------------
  // FrameEx parameters (0-100 sliders)
  // -------------------------------------------------------------------------
  int creakiness = 0;             // 0.0-1.0, default 0
  int breathiness = 0;            // 0.0-1.0, default 0
  int jitter = 0;                 // 0.0-1.0, default 0
  int shimmer = 0;                // 0.0-1.0, default 0
  int sharpness = 50;             // 0.5-2.0 multiplier, default 1.0

  bool help = false;
  bool listVoices = false;        // --list-voices: print available voice profiles and exit
};

// ============================================================================
// Helpers
// ============================================================================

static void printHelp(const char* argv0) {
  std::cerr
    << "Usage: " << (argv0 ? argv0 : "nvspRender") << " [options]\n\n"
    << "Reads IPA text from stdin (UTF-8) and writes raw 16-bit PCM to stdout.\n\n"
    << "Basic options:\n"
    << "  --packdir <path>      Path to repo root or packs dir (default: .)\n"
    << "  --lang <tag>          Language tag for pack selection (default: en)\n"
    << "  --voice <name>        Voice profile name (loads voicingTone from YAML)\n"
    << "  --list-voices         List available voice profiles and exit\n"
    << "  --rate <int>          SSIP-style rate -100..100 (default: 0)\n"
    << "  --pitch <int>         Pitch 0..100 (default: 50)\n"
    << "  --volume <float>      Output gain multiplier (default: 1.0)\n"
    << "  --samplerate <int>    Output sample rate in Hz (default: 16000)\n"
    << "  --inflection <float>  Inflection amount (default: 0.5)\n"
    << "\n"
    << "VoicingTone parameters (0-100 sliders):\n"
    << "  --voicing-peak-pos <int>       Glottal pulse peak position (default: 50)\n"
    << "  --voiced-preemph-a <int>       Pre-emphasis coefficient (default: 50)\n"
    << "  --voiced-preemph-mix <int>     Pre-emphasis mix (default: 50)\n"
    << "  --high-shelf-gain <int>        High shelf gain dB (default: 50)\n"
    << "  --high-shelf-fc <int>          High shelf frequency (default: 50)\n"
    << "  --high-shelf-q <int>           High shelf Q (default: 50)\n"
    << "  --voiced-tilt <int>            Voiced spectral tilt dB/oct (default: 50)\n"
    << "  --noise-glottal-mod <int>      Noise glottal modulation depth (default: 0)\n"
    << "  --pitch-sync-f1 <int>          Pitch-sync F1 delta Hz (default: 50)\n"
    << "  --pitch-sync-b1 <int>          Pitch-sync B1 delta Hz (default: 50)\n"
    << "  --speed-quotient <int>         Glottal pulse asymmetry (default: 50)\n"
    << "  --aspiration-tilt <int>        Aspiration spectral tilt (default: 50)\n"
    << "  --cascade-bw-scale <int>       Formant sharpness (cascade bandwidth) (default: 50)\n"
    << "  --formant-sharpness <int>      Formant sharpness (cascade bandwidth, default: 50)\n"
    << "  --tremor <int>                 Voice tremor / shakiness (default: 0)\n"
    << "\n"
    << "FrameEx voice quality parameters (0-100 sliders):\n"
    << "  --creakiness <int>    Laryngealization / creaky voice (default: 0)\n"
    << "  --breathiness <int>   Breath noise in voicing (default: 0)\n"
    << "  --jitter <int>        Pitch period variation (default: 0)\n"
    << "  --shimmer <int>       Amplitude variation (default: 0)\n"
    << "  --sharpness <int>     Glottal closure sharpness (default: 50)\n"
    << "\n"
    << "  -h, --help            Show this help\n";
}

static bool parseInt(const char* s, int& out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  long v = std::strtol(s, &end, 10);
  if (!end || *end != '\0') return false;
  out = static_cast<int>(v);
  return true;
}

static bool parseDouble(const char* s, double& out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  double v = std::strtod(s, &end);
  if (!end || *end != '\0') return false;
  out = v;
  return true;
}

static int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static Options parseArgs(int argc, char** argv) {
  Options opt;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i] ? argv[i] : "";

    if (a == "-h" || a == "--help") {
      opt.help = true;
      continue;
    }
    if (a == "--list-voices") {
      opt.listVoices = true;
      continue;
    }

    auto requireValue = [&](const char* name) -> const char* {
      if (i + 1 >= argc || !argv[i + 1]) {
        std::cerr << "Missing value for " << name << "\n";
        opt.help = true;
        return nullptr;
      }
      return argv[++i];
    };

    auto parseIntArg = [&](const char* name, int& target) {
      if (const char* v = requireValue(name)) {
        int tmp = 0;
        if (!parseInt(v, tmp)) {
          std::cerr << "Bad " << name << " value: " << v << "\n";
          opt.help = true;
        } else {
          target = tmp;
        }
      }
    };

    auto parseDoubleArg = [&](const char* name, double& target) {
      if (const char* v = requireValue(name)) {
        double tmp = 0;
        if (!parseDouble(v, tmp)) {
          std::cerr << "Bad " << name << " value: " << v << "\n";
          opt.help = true;
        } else {
          target = tmp;
        }
      }
    };

    // Basic options
    if (a == "--packdir") {
      if (const char* v = requireValue("--packdir")) opt.packDir = v;
      continue;
    }
    if (a == "--lang") {
      if (const char* v = requireValue("--lang")) opt.language = v;
      continue;
    }
    if (a == "--voice") {
      if (const char* v = requireValue("--voice")) opt.voiceProfile = v;
      continue;
    }
    if (a == "--rate") { parseIntArg("--rate", opt.rate); continue; }
    if (a == "--pitch") { parseIntArg("--pitch", opt.pitch); continue; }
    if (a == "--volume") { parseDoubleArg("--volume", opt.volume); continue; }
    if (a == "--samplerate" || a == "--sample-rate") {
      if (const char* v = requireValue(a.c_str())) {
        int tmp = 16000;
        if (!parseInt(v, tmp) || tmp < 8000 || tmp > 192000) {
          std::cerr << "Bad --samplerate value: " << v << " (expected 8000..192000)\n";
          opt.help = true;
        } else {
          opt.sampleRate = tmp;
        }
      }
      continue;
    }
    if (a == "--inflection") { parseDoubleArg("--inflection", opt.inflection); continue; }

    // VoicingTone parameters
    if (a == "--voicing-peak-pos") { parseIntArg(a.c_str(), opt.voicingPeakPos); continue; }
    if (a == "--voiced-preemph-a") { parseIntArg(a.c_str(), opt.voicedPreEmphA); continue; }
    if (a == "--voiced-preemph-mix") { parseIntArg(a.c_str(), opt.voicedPreEmphMix); continue; }
    if (a == "--high-shelf-gain") { parseIntArg(a.c_str(), opt.highShelfGainDb); continue; }
    if (a == "--high-shelf-fc") { parseIntArg(a.c_str(), opt.highShelfFcHz); continue; }
    if (a == "--high-shelf-q") { parseIntArg(a.c_str(), opt.highShelfQ); continue; }
    if (a == "--voiced-tilt") { parseIntArg(a.c_str(), opt.voicedTiltDbPerOct); continue; }
    if (a == "--noise-glottal-mod") { parseIntArg(a.c_str(), opt.noiseGlottalModDepth); continue; }
    if (a == "--pitch-sync-f1") { parseIntArg(a.c_str(), opt.pitchSyncF1DeltaHz); continue; }
    if (a == "--pitch-sync-b1") { parseIntArg(a.c_str(), opt.pitchSyncB1DeltaHz); continue; }
    if (a == "--speed-quotient") { parseIntArg(a.c_str(), opt.speedQuotient); continue; }
    if (a == "--aspiration-tilt") { parseIntArg(a.c_str(), opt.aspirationTiltDbPerOct); continue; }
    if (a == "--cascade-bw-scale" || a == "--formant-sharpness") { parseIntArg(a.c_str(), opt.cascadeBwScale); continue; }
    if (a == "--tremor") { parseIntArg(a.c_str(), opt.tremor); continue; }

    // FrameEx parameters
    if (a == "--creakiness") { parseIntArg(a.c_str(), opt.creakiness); continue; }
    if (a == "--breathiness") { parseIntArg(a.c_str(), opt.breathiness); continue; }
    if (a == "--jitter") { parseIntArg(a.c_str(), opt.jitter); continue; }
    if (a == "--shimmer") { parseIntArg(a.c_str(), opt.shimmer); continue; }
    if (a == "--sharpness") { parseIntArg(a.c_str(), opt.sharpness); continue; }

    std::cerr << "Unknown arg: " << a << "\n";
    opt.help = true;
  }

  return opt;
}

static std::string readAllStdin() {
  std::string out;
  char buf[4096];
  while (true) {
    size_t n = std::fread(buf, 1, sizeof(buf), stdin);
    if (n > 0) out.append(buf, n);
    if (n < sizeof(buf)) {
      if (std::feof(stdin)) break;
      if (std::ferror(stdin)) break;
    }
  }
  return out;
}

// ============================================================================
// VoicingTone V3 builder (slider 0-100 -> actual values)
// ============================================================================

static VoicingToneV3 buildVoicingTone(const Options& opt) {
  VoicingToneV3 tone{};
  
  tone.magic = SPEECHPLAYER_VOICINGTONE_MAGIC;
  tone.structSize = sizeof(VoicingToneV3);
  tone.structVersion = SPEECHPLAYER_VOICINGTONE_VERSION;
  tone.dspVersion = SPEECHPLAYER_DSP_VERSION;

  auto slider = [](int v) { return static_cast<double>(clampInt(v, 0, 100)) / 100.0; };

  // Map sliders to actual values (same mapping as NVDA driver and phoneme editor)
  tone.voicingPeakPos = 0.85 + slider(opt.voicingPeakPos) * 0.10;           // 0.85-0.95
  tone.voicedPreEmphA = slider(opt.voicedPreEmphA) * 0.97;                  // 0.0-0.97
  tone.voicedPreEmphMix = slider(opt.voicedPreEmphMix);                     // 0.0-1.0
  tone.highShelfGainDb = -12.0 + slider(opt.highShelfGainDb) * 24.0;        // -12 to +12
  tone.highShelfFcHz = 500.0 + slider(opt.highShelfFcHz) * 7500.0;          // 500-8000
  tone.highShelfQ = 0.3 + slider(opt.highShelfQ) * 1.7;                     // 0.3-2.0
  tone.voicedTiltDbPerOct = -24.0 + slider(opt.voicedTiltDbPerOct) * 48.0;  // -24 to +24
  tone.noiseGlottalModDepth = slider(opt.noiseGlottalModDepth);             // 0.0-1.0
  tone.pitchSyncF1DeltaHz = -60.0 + slider(opt.pitchSyncF1DeltaHz) * 120.0; // -60 to +60
  tone.pitchSyncB1DeltaHz = -50.0 + slider(opt.pitchSyncB1DeltaHz) * 100.0; // -50 to +50
  tone.speedQuotient = 0.5 + slider(opt.speedQuotient) * 3.5;               // 0.5-4.0
  tone.aspirationTiltDbPerOct = -12.0 + slider(opt.aspirationTiltDbPerOct) * 24.0; // -12 to +12
  // cascadeBwScale: piecewise so that 50 => 1.0
  {
    const int s = clampInt(opt.cascadeBwScale, 0, 100);
    if (s <= 50) tone.cascadeBwScale = 0.4 + (static_cast<double>(s) / 50.0) * 0.6;
    else tone.cascadeBwScale = 1.0 + (static_cast<double>(s - 50) / 50.0) * 0.4;
  }
  // tremorDepth: 0-100 maps to 0.0-0.4
  tone.tremorDepth = slider(opt.tremor) * 0.4;

  return tone;
}

// ============================================================================
// FrameEx builder (slider 0-100 -> actual values)
// ============================================================================

static FrameEx buildFrameEx(const Options& opt, bool& outHasEffect) {
  FrameEx ex{};
  outHasEffect = false;

  auto slider = [](int v) { return static_cast<double>(clampInt(v, 0, 100)) / 100.0; };

  ex.creakiness = slider(opt.creakiness);
  ex.breathiness = slider(opt.breathiness);
  ex.jitter = slider(opt.jitter);
  ex.shimmer = slider(opt.shimmer);
  // sharpness: 0-100 -> 0.5-2.0 multiplier (50 = 1.0 = neutral)
  ex.sharpness = 0.5 + slider(opt.sharpness) * 1.5;

  // Check if any effect is active
  outHasEffect = (opt.creakiness > 0 || opt.breathiness > 0 || 
                  opt.jitter > 0 || opt.shimmer > 0 || opt.sharpness != 50);

  return ex;
}

// ============================================================================
// Callback context
// ============================================================================

struct CallbackCtx {
  speechPlayer_handle_t player = nullptr;
  int sampleRate = 16000;
  double volume = 1.0;
  FrameEx userFrameEx{};      // User-level defaults from CLI (additive)
  bool hasUserFrameEx = false;
};

static void onFrontendFrameEx(
    void* userData,
    const nvspFrontend_Frame* frameOrNull,
    const nvspFrontend_FrameEx* frameExOrNull,  // Per-phoneme FrameEx (e.g. Danish stød)
    double durationMs,
    double fadeMs,
    int userIndex
) {
  auto* ctx = static_cast<CallbackCtx*>(userData);
  if (!ctx || !ctx->player) return;

  auto msToSamples = [&](double ms) -> unsigned int {
    if (ms <= 0.0) return 0;
    const double s = (ms * static_cast<double>(ctx->sampleRate)) / 1000.0;
    if (s <= 0.0) return 0;
    return static_cast<unsigned int>(s + 0.5);
  };

  const unsigned int minSamples = msToSamples(durationMs);
  const unsigned int fadeSamples = msToSamples(fadeMs);

  if (frameOrNull) {
    static_assert(sizeof(nvspFrontend_Frame) == sizeof(speechPlayer_frame_t), "Frame ABI mismatch");
    speechPlayer_frame_t f{};
    std::memcpy(&f, frameOrNull, sizeof(f));
    f.outputGain *= ctx->volume;

    // Use FrameEx if we have per-phoneme values OR user CLI overrides
    if (frameExOrNull || ctx->hasUserFrameEx) {
      FrameEx merged{};
      
      // Start with per-phoneme values from frontend (includes Fujisaki pitch model)
      if (frameExOrNull) {
        // Copy all 18 fields - frontend provides formant ramping and Fujisaki data
        std::memcpy(&merged, frameExOrNull, sizeof(FrameEx));
      } else {
        merged.sharpness = 1.0;  // Neutral default for sharpness
        // Formant end targets: 0.0 is fine (DSP treats as "no target")
        // Fujisaki fields: 0.0 means disabled
      }
      
      // Add user CLI overrides for voice quality params only
      // (additive for 0-1 params, multiplicative for sharpness)
      if (ctx->hasUserFrameEx) {
        merged.creakiness = std::min(1.0, merged.creakiness + ctx->userFrameEx.creakiness);
        merged.breathiness = std::min(1.0, merged.breathiness + ctx->userFrameEx.breathiness);
        merged.jitter = std::min(1.0, merged.jitter + ctx->userFrameEx.jitter);
        merged.shimmer = std::min(1.0, merged.shimmer + ctx->userFrameEx.shimmer);
        merged.sharpness *= ctx->userFrameEx.sharpness;
        // Note: formant end targets and Fujisaki params come from frontend only,
        // no CLI overrides for those (they're per-phoneme/per-utterance)
      }
      
      speechPlayer_queueFrameEx(ctx->player, &f,
                                reinterpret_cast<const speechPlayer_frameEx_t*>(&merged),
                                static_cast<unsigned int>(sizeof(FrameEx)),
                                minSamples, fadeSamples, userIndex, false);
    } else {
      speechPlayer_queueFrame(ctx->player, &f, minSamples, fadeSamples, userIndex, false);
    }
  } else {
    // Silence frame - no FrameEx needed
    speechPlayer_queueFrame(ctx->player, nullptr, minSamples, fadeSamples, userIndex, false);
  }
}

// Map Speech Dispatcher SSIP rate (-100..+100) to a speed multiplier.
static double ssipRateToSpeed(int ssipRate) {
  if (ssipRate < -100) ssipRate = -100;
  if (ssipRate > 100) ssipRate = 100;
  return std::pow(2.0, static_cast<double>(ssipRate) / 100.0);
}

// Map a 0..100 pitch slider to a base pitch in Hz.
static double sliderPitchToBaseHz(int pitch0to100) {
  if (pitch0to100 < 0) pitch0to100 = 0;
  if (pitch0to100 > 100) pitch0to100 = 100;
  return 25.0 + (21.25 * (static_cast<double>(pitch0to100) / 12.5));
}

// Check if any VoicingTone parameter is non-default
static bool hasVoicingToneEffect(const Options& opt) {
  return (opt.voicingPeakPos != 50 || opt.voicedPreEmphA != 50 ||
          opt.voicedPreEmphMix != 50 || opt.highShelfGainDb != 50 ||
          opt.highShelfFcHz != 50 || opt.highShelfQ != 50 ||
          opt.voicedTiltDbPerOct != 50 || opt.noiseGlottalModDepth != 0 ||
          opt.pitchSyncF1DeltaHz != 50 || opt.pitchSyncB1DeltaHz != 50 ||
          opt.speedQuotient != 50 || opt.aspirationTiltDbPerOct != 50 ||
          opt.cascadeBwScale != 50 || opt.tremor != 0);
}

}  // namespace

int main(int argc, char** argv) {
  const Options opt = parseArgs(argc, argv);
  if (opt.help) {
    printHelp(argv && argv[0] ? argv[0] : "nvspRender");
    return 2;
  }

  // Handle --list-voices: print available profiles and exit
  if (opt.listVoices) {
    nvspFrontend_handle_t fe = nvspFrontend_create(opt.packDir.c_str());
    if (!fe) {
      std::cerr << "nvspFrontend_create failed (packDir='" << opt.packDir << "')\n";
      return 1;
    }
    if (!nvspFrontend_setLanguage(fe, opt.language.c_str())) {
      std::cerr << "nvspFrontend_setLanguage failed (lang='" << opt.language << "')\n";
      nvspFrontend_destroy(fe);
      return 1;
    }
    
    const char* names = nvspFrontend_getVoiceProfileNames(fe);
    if (names && *names) {
      std::cerr << "Available voice profiles:\n";
      std::string nameStr = names;
      std::string::size_type pos = 0, prev = 0;
      while ((pos = nameStr.find('\n', prev)) != std::string::npos) {
        std::string name = nameStr.substr(prev, pos - prev);
        if (!name.empty()) {
          std::cerr << "  " << name << "\n";
        }
        prev = pos + 1;
      }
      if (prev < nameStr.size()) {
        std::cerr << "  " << nameStr.substr(prev) << "\n";
      }
      std::cerr << "\nExample speech-dispatcher AddVoice lines:\n";
      prev = 0;
      while ((pos = nameStr.find('\n', prev)) != std::string::npos) {
        std::string name = nameStr.substr(prev, pos - prev);
        if (!name.empty()) {
          std::cerr << "  AddVoice \"en\" \"male1\" \"" << name << "\"\n";
        }
        prev = pos + 1;
      }
      if (prev < nameStr.size()) {
        std::cerr << "  AddVoice \"en\" \"male1\" \"" << nameStr.substr(prev) << "\"\n";
      }
    } else {
      std::cerr << "No voice profiles found.\n";
    }
    nvspFrontend_destroy(fe);
    return 0;
  }

#if defined(_WIN32)
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  const std::string ipa = readAllStdin();
  if (ipa.empty()) {
    return 0;
  }

  // Initialize speechPlayer with the requested sample rate
  const int sampleRate = opt.sampleRate;
  speechPlayer_handle_t player = speechPlayer_initialize(sampleRate);
  if (!player) {
    std::cerr << "speechPlayer_initialize failed\n";
    return 1;
  }

  // Initialize frontend
  nvspFrontend_handle_t fe = nvspFrontend_create(opt.packDir.c_str());
  if (!fe) {
    std::cerr << "nvspFrontend_create failed (packDir='" << opt.packDir << "')\n";
    speechPlayer_terminate(player);
    return 1;
  }

  if (!nvspFrontend_setLanguage(fe, opt.language.c_str())) {
    std::cerr << "nvspFrontend_setLanguage failed (lang='" << opt.language << "')\n";
    const char* err = nvspFrontend_getLastError(fe);
    if (err && *err) std::cerr << "  " << err << "\n";
    nvspFrontend_destroy(fe);
    speechPlayer_terminate(player);
    return 1;
  }

  // Set voice profile if specified
  if (!opt.voiceProfile.empty()) {
    if (!nvspFrontend_setVoiceProfile(fe, opt.voiceProfile.c_str())) {
      std::cerr << "nvspFrontend_setVoiceProfile failed (voice='" << opt.voiceProfile << "')\n";
      const char* err = nvspFrontend_getLastError(fe);
      if (err && *err) std::cerr << "  " << err << "\n";
      // Continue anyway - fall back to default voice
    }
  }

  // Apply VoicingTone: first try to load from YAML via frontend, then apply CLI overrides
  {
    VoicingToneV3 tone{};
    tone.magic = SPEECHPLAYER_VOICINGTONE_MAGIC;
    tone.structSize = sizeof(VoicingToneV3);
    tone.structVersion = SPEECHPLAYER_VOICINGTONE_VERSION;
    tone.dspVersion = SPEECHPLAYER_DSP_VERSION;
    
    // Start with defaults
    tone.voicingPeakPos = 0.91;
    tone.voicedPreEmphA = 0.92;
    tone.voicedPreEmphMix = 0.35;
    tone.highShelfGainDb = 2.0;
    tone.highShelfFcHz = 2800.0;
    tone.highShelfQ = 0.7;
    tone.voicedTiltDbPerOct = 0.0;
    tone.noiseGlottalModDepth = 0.0;
    tone.pitchSyncF1DeltaHz = 0.0;
    tone.pitchSyncB1DeltaHz = 0.0;
    tone.speedQuotient = 2.0;
    tone.aspirationTiltDbPerOct = 0.0;
    tone.cascadeBwScale = 1.0;
    tone.tremorDepth = 0.0;
    
    // Try to get voicing tone from YAML (if voice profile has one)
    nvspFrontend_VoicingTone yamlTone{};
    if (nvspFrontend_getVoicingTone(fe, &yamlTone)) {
      // Copy YAML values
      tone.voicingPeakPos = yamlTone.voicingPeakPos;
      tone.voicedPreEmphA = yamlTone.voicedPreEmphA;
      tone.voicedPreEmphMix = yamlTone.voicedPreEmphMix;
      tone.highShelfGainDb = yamlTone.highShelfGainDb;
      tone.highShelfFcHz = yamlTone.highShelfFcHz;
      tone.highShelfQ = yamlTone.highShelfQ;
      tone.voicedTiltDbPerOct = yamlTone.voicedTiltDbPerOct;
      tone.noiseGlottalModDepth = yamlTone.noiseGlottalModDepth;
      tone.pitchSyncF1DeltaHz = yamlTone.pitchSyncF1DeltaHz;
      tone.pitchSyncB1DeltaHz = yamlTone.pitchSyncB1DeltaHz;
      tone.speedQuotient = yamlTone.speedQuotient;
      tone.aspirationTiltDbPerOct = yamlTone.aspirationTiltDbPerOct;
      tone.cascadeBwScale = yamlTone.cascadeBwScale;
      tone.tremorDepth = yamlTone.tremorDepth;
    }
    
    // Apply CLI overrides (only if non-default)
    if (hasVoicingToneEffect(opt)) {
      VoicingToneV3 cliTone = buildVoicingTone(opt);
      // CLI args override YAML values
      if (opt.voicingPeakPos != 50) tone.voicingPeakPos = cliTone.voicingPeakPos;
      if (opt.voicedPreEmphA != 50) tone.voicedPreEmphA = cliTone.voicedPreEmphA;
      if (opt.voicedPreEmphMix != 50) tone.voicedPreEmphMix = cliTone.voicedPreEmphMix;
      if (opt.highShelfGainDb != 50) tone.highShelfGainDb = cliTone.highShelfGainDb;
      if (opt.highShelfFcHz != 50) tone.highShelfFcHz = cliTone.highShelfFcHz;
      if (opt.highShelfQ != 50) tone.highShelfQ = cliTone.highShelfQ;
      if (opt.voicedTiltDbPerOct != 50) tone.voicedTiltDbPerOct = cliTone.voicedTiltDbPerOct;
      if (opt.noiseGlottalModDepth != 0) tone.noiseGlottalModDepth = cliTone.noiseGlottalModDepth;
      if (opt.pitchSyncF1DeltaHz != 50) tone.pitchSyncF1DeltaHz = cliTone.pitchSyncF1DeltaHz;
      if (opt.pitchSyncB1DeltaHz != 50) tone.pitchSyncB1DeltaHz = cliTone.pitchSyncB1DeltaHz;
      if (opt.speedQuotient != 50) tone.speedQuotient = cliTone.speedQuotient;
      if (opt.aspirationTiltDbPerOct != 50) tone.aspirationTiltDbPerOct = cliTone.aspirationTiltDbPerOct;
      if (opt.cascadeBwScale != 50) tone.cascadeBwScale = cliTone.cascadeBwScale;
      if (opt.tremor != 0) tone.tremorDepth = cliTone.tremorDepth;
    }
    
    speechPlayer_setVoicingTone(player, reinterpret_cast<const speechPlayer_voicingTone_t*>(&tone));
  }

  // Build user-level FrameEx defaults from CLI args
  bool hasUserFrameEx = false;
  FrameEx userFrameEx = buildFrameEx(opt, hasUserFrameEx);

  CallbackCtx cbCtx;
  cbCtx.player = player;
  cbCtx.sampleRate = sampleRate;
  cbCtx.volume = opt.volume;
  cbCtx.userFrameEx = userFrameEx;
  cbCtx.hasUserFrameEx = hasUserFrameEx;

  const double speed = ssipRateToSpeed(opt.rate);
  const double basePitchHz = sliderPitchToBaseHz(opt.pitch);
  const double inflection = opt.inflection;

  const char* clauseTypeUtf8 = nullptr;

  // Use the extended API to get per-phoneme FrameEx (e.g. Danish stød creakiness)
  if (!nvspFrontend_queueIPA_Ex(
    fe,
    ipa.c_str(),
    speed,
    basePitchHz,
    inflection,
    clauseTypeUtf8,
    /*userIndexBase=*/0,
    &onFrontendFrameEx,
    &cbCtx
  )) {
    std::cerr << "nvspFrontend_queueIPA_Ex failed\n";
    const char* err = nvspFrontend_getLastError(fe);
    if (err && *err) std::cerr << "  " << err << "\n";
    nvspFrontend_destroy(fe);
    speechPlayer_terminate(player);
    return 1;
  }

  // Synthesize to stdout as raw PCM
  static_assert(sizeof(sample) == sizeof(sampleVal), "sample struct should be a packed 16-bit value");
  std::vector<sample> pcm;
  pcm.resize(2048);

  while (true) {
    const int n = speechPlayer_synthesize(player, static_cast<unsigned int>(pcm.size()), pcm.data());
    if (n <= 0) break;

    const size_t bytes = static_cast<size_t>(n) * sizeof(sample);
    size_t written = 0;
    while (written < bytes) {
      const size_t w = std::fwrite(reinterpret_cast<const char*>(pcm.data()) + written, 1, bytes - written, stdout);
      if (w == 0) {
        nvspFrontend_destroy(fe);
        speechPlayer_terminate(player);
        return 0;
      }
      written += w;
    }

    if (n < static_cast<int>(pcm.size())) break;
  }

  nvspFrontend_destroy(fe);
  speechPlayer_terminate(player);
  return 0;
}