/*
  nvspRender
  ---------
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
    - VoicingTone V3 support (12 parameters)
    - FrameEx support (creakiness, breathiness, jitter, shimmer, sharpness)
    - Voice profile support via nvspFrontend_setVoiceProfile
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
#define SPEECHPLAYER_DSP_VERSION 5u
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
};

// ============================================================================
// FrameEx structure (must match frame.h)
// ============================================================================

struct FrameEx {
  double creakiness;
  double breathiness;
  double jitter;
  double shimmer;
  double sharpness;
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

  // -------------------------------------------------------------------------
  // FrameEx parameters (0-100 sliders)
  // -------------------------------------------------------------------------
  int creakiness = 0;             // 0.0-1.0, default 0
  int breathiness = 0;            // 0.0-1.0, default 0
  int jitter = 0;                 // 0.0-1.0, default 0
  int shimmer = 0;                // 0.0-1.0, default 0
  int sharpness = 50;             // 0.5-2.0 multiplier, default 1.0

  bool help = false;
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
    << "  --voice <name>        Voice profile name (default: none)\n"
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
  FrameEx frameEx{};
  bool useFrameEx = false;
};

static void onFrontendFrame(
    void* userData,
    const nvspFrontend_Frame* frameOrNull,
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

    if (ctx->useFrameEx) {
      speechPlayer_queueFrameEx(ctx->player, &f, 
                                reinterpret_cast<const speechPlayer_frameEx_t*>(&ctx->frameEx),
                                static_cast<unsigned int>(sizeof(FrameEx)),
                                minSamples, fadeSamples, userIndex, false);
    } else {
      speechPlayer_queueFrame(ctx->player, &f, minSamples, fadeSamples, userIndex, false);
    }
  } else {
    // Silence frame
    if (ctx->useFrameEx) {
      speechPlayer_queueFrameEx(ctx->player, nullptr,
                                reinterpret_cast<const speechPlayer_frameEx_t*>(&ctx->frameEx),
                                static_cast<unsigned int>(sizeof(FrameEx)),
                                minSamples, fadeSamples, userIndex, false);
    } else {
      speechPlayer_queueFrame(ctx->player, nullptr, minSamples, fadeSamples, userIndex, false);
    }
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
          opt.speedQuotient != 50 || opt.aspirationTiltDbPerOct != 50);
}

}  // namespace

int main(int argc, char** argv) {
  const Options opt = parseArgs(argc, argv);
  if (opt.help) {
    printHelp(argv && argv[0] ? argv[0] : "nvspRender");
    return 2;
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

  // Apply VoicingTone if any parameters are non-default
  if (hasVoicingToneEffect(opt)) {
    VoicingToneV3 tone = buildVoicingTone(opt);
    speechPlayer_setVoicingTone(player, reinterpret_cast<const speechPlayer_voicingTone_t*>(&tone));
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

  // Build FrameEx
  bool useFrameEx = false;
  FrameEx frameEx = buildFrameEx(opt, useFrameEx);

  CallbackCtx cbCtx;
  cbCtx.player = player;
  cbCtx.sampleRate = sampleRate;
  cbCtx.volume = opt.volume;
  cbCtx.frameEx = frameEx;
  cbCtx.useFrameEx = useFrameEx;

  const double speed = ssipRateToSpeed(opt.rate);
  const double basePitchHz = sliderPitchToBaseHz(opt.pitch);
  const double inflection = opt.inflection;

  const char* clauseTypeUtf8 = nullptr;

  if (!nvspFrontend_queueIPA(
    fe,
    ipa.c_str(),
    speed,
    basePitchHz,
    inflection,
    clauseTypeUtf8,
    /*userIndexBase=*/0,
    &onFrontendFrame,
    &cbCtx
  )) {
    std::cerr << "nvspFrontend_queueIPA failed\n";
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