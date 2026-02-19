/*
TGSpeechBox — Language pack data structures and phoneme definitions.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_PACK_H
#define TGSB_FRONTEND_PACK_H

#include <cmath>
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "yaml_min.h"
#include "utf8.h"
#include "voice_profile.h"

namespace nvsp_frontend {

// Field count matches nvspFrontend_Frame in nvspFrontend.h (47 doubles).
constexpr int kFrameFieldCount = 47;

// Frame field IDs by index (must match struct order).
enum class FieldId : int {
  voicePitch = 0,
  vibratoPitchOffset = 1,
  vibratoSpeed = 2,
  voiceTurbulenceAmplitude = 3,
  glottalOpenQuotient = 4,
  voiceAmplitude = 5,
  aspirationAmplitude = 6,
  cf1 = 7,
  cf2 = 8,
  cf3 = 9,
  cf4 = 10,
  cf5 = 11,
  cf6 = 12,
  cfN0 = 13,
  cfNP = 14,
  cb1 = 15,
  cb2 = 16,
  cb3 = 17,
  cb4 = 18,
  cb5 = 19,
  cb6 = 20,
  cbN0 = 21,
  cbNP = 22,
  caNP = 23,
  fricationAmplitude = 24,
  pf1 = 25,
  pf2 = 26,
  pf3 = 27,
  pf4 = 28,
  pf5 = 29,
  pf6 = 30,
  pb1 = 31,
  pb2 = 32,
  pb3 = 33,
  pb4 = 34,
  pb5 = 35,
  pb6 = 36,
  pa1 = 37,
  pa2 = 38,
  pa3 = 39,
  pa4 = 40,
  pa5 = 41,
  pa6 = 42,
  parallelBypass = 43,
  preFormantGain = 44,
  outputGain = 45,
  endVoicePitch = 46,
};

// Phoneme flags (based on data.py keys).
enum PhonemeFlagBits : std::uint32_t {
  kIsAfricate   = 1u << 0,
  kIsLiquid     = 1u << 1,
  kIsNasal      = 1u << 2,
  kIsSemivowel  = 1u << 3,
  kIsStop       = 1u << 4,
  kIsTap        = 1u << 5,
  kIsTrill      = 1u << 6,
  kIsVoiced     = 1u << 7,
  kIsVowel      = 1u << 8,
  kCopyAdjacent = 1u << 9,
};

struct PhonemeDef {
  std::u32string key;
  std::uint32_t flags = 0;
  // Which frame fields are explicitly specified in YAML.
  std::uint64_t setMask = 0;
  double field[kFrameFieldCount] = {0.0};
  
  // Per-phoneme FrameEx overrides (voice quality).
  // The has* flags indicate which values are explicitly set in YAML.
  // Values without has* true should fall back to user defaults.
  bool hasCreakiness = false;
  bool hasBreathiness = false;
  bool hasJitter = false;
  bool hasShimmer = false;
  bool hasSharpness = false;
  
  // Formant end targets (for within-frame ramping)
  bool hasEndCf1 = false;
  bool hasEndCf2 = false;
  bool hasEndCf3 = false;
  bool hasEndPf1 = false;
  bool hasEndPf2 = false;
  bool hasEndPf3 = false;
  
  double creakiness = 0.0;    // 0.0-1.0, additive with user default
  double breathiness = 0.0;   // 0.0-1.0, additive with user default
  double jitter = 0.0;        // 0.0-1.0, additive with user default
  double shimmer = 0.0;       // 0.0-1.0, additive with user default
  double sharpness = 1.0;     // multiplier (1.0 = neutral, only >= 1.0 used)
  
  // Formant end targets in Hz (NAN = no ramping)
  double endCf1 = NAN;
  double endCf2 = NAN;
  double endCf3 = NAN;
  double endPf1 = NAN;
  double endPf2 = NAN;
  double endPf3 = NAN;

  // ── Micro-event fields (consumed by frame_emit, NOT by DSP) ────────
  // These shape amplitude envelopes within a token. The DSP sees the
  // same parameters it always has — just with time-varying values.
  // All are optional; omitted fields use flag-gated defaults.

  // Stop burst shaping (only read when kIsStop or kIsAffricate is set):
  bool hasBurstDurationMs = false;
  double burstDurationMs = 0.0;      // how long burst energy lasts (ms)

  bool hasBurstDecayRate = false;
  double burstDecayRate = 0.0;       // 0.0=flat, 1.0=instant decay (default 0.5)

  bool hasBurstSpectralTilt = false;
  double burstSpectralTilt = 0.0;    // negative=boost pa5/pa6, positive=boost pa3/pa4

  // Voiced stop closure (only read when kIsStop+kIsVoiced):
  bool hasVoiceBarAmplitude = false;
  double voiceBarAmplitude = 0.0;    // voicing energy during closure (default 0.3 voiced, 0.0 voiceless)

  bool hasVoiceBarF1 = false;
  double voiceBarF1 = 0.0;          // F1 during voice bar (default 150 Hz)

  // Aspiration/release shaping (only read when postStopAspiration token):
  bool hasReleaseSpreadMs = false;
  double releaseSpreadMs = 0.0;      // how gradually aspiration ramps in after burst (default 4)

  // Fricative envelope (only read when fricationAmplitude > 0):
  bool hasFricAttackMs = false;
  double fricAttackMs = 0.0;         // onset ramp time for frication (default 3)

  bool hasFricDecayMs = false;
  double fricDecayMs = 0.0;          // offset ramp time for frication (default 4)

  // Duration scaling — multiplier on the class-default duration.
  // A velar stop with durationScale=1.3 and stopDurationMs=6.0 gets 7.8ms.
  // Default 1.0 (no change). Consumed by ipa_engine timing, not frame_emit.
  double durationScale = 1.0;
  bool hasDurationScale = false;
};

// In YAML we keep replacements in UTF-8; we convert to UTF-32 during load.
struct RuleWhen {
  bool atWordStart = false;
  bool atWordEnd = false;
  std::string beforeClass;    // name from classes - match only if next char is in class
  std::string afterClass;     // name from classes - match only if prev char is in class
  std::string notBeforeClass; // name from classes - match only if next char is NOT in class
  std::string notAfterClass;  // name from classes - match only if prev char is NOT in class
};

struct ReplacementRule {
  std::u32string from;
  std::vector<std::u32string> to; // first supported one is chosen
  RuleWhen when;
};

struct TransformRule {
  // Match on phoneme properties.
  // Any field left as -1 means "don't care".
  int isVowel = -1;
  int isVoiced = -1;
  int isStop = -1;
  int isAfricate = -1;
  int isNasal = -1;
  int isLiquid = -1;
  int isSemivowel = -1;
  int isTap = -1;
  int isTrill = -1;
  int isFricativeLike = -1; // derived from fricationAmplitude

  // Operations on frame fields.
  std::unordered_map<FieldId, double> set;
  std::unordered_map<FieldId, double> scale;
  std::unordered_map<FieldId, double> add;
};

struct IntonationClause {
  // Percent values, same semantics as ipa_convert.py.
  int preHeadStart = 46;
  int preHeadEnd = 57;
  int headExtendFrom = 4;
  int headStart = 80;
  int headEnd = 50;
  std::vector<int> headSteps;
  int headStressEndDelta = -16;
  int headUnstressedRunStartDelta = -8;
  int headUnstressedRunEndDelta = -5;
  int nucleus0Start = 64;
  int nucleus0End = 8;
  int nucleusStart = 70;
  int nucleusEnd = 18;
  int tailStart = 24;
  int tailEnd = 8;
};


struct SpecialCoarticRule {
  std::string name;
  std::vector<std::string> triggers;  // IPA keys e.g. "ɹ", "r", "w"
  std::string vowelFilter;            // "all", "front", "back", or specific IPA key
  std::string formant;                // "f2", "f3"
  double deltaHz = 0.0;
  std::string side;                   // "left", "right", "both"
  bool cumulative = false;            // apply from both sides additively
  double unstressedScale = 1.0;       // multiply delta for unstressed vowels
  double phraseFinalStressedScale = 1.0; // multiply delta for phrase-final stressed
};

struct AllophoneRule {
    std::string name;

    // ── Match conditions ──────────────────────────────────────────────
    std::vector<std::u32string> phonemes;   // IPA keys (empty = match by flags only)
    std::vector<std::string> flags;         // ALL listed flags must be present
    std::vector<std::string> notFlags;      // exclude if ANY listed flag present

    // "phoneme" (default), "aspiration", "closure"
    std::string tokenType = "phoneme";

    // "any", "word-initial", "word-final", "intervocalic",
    // "pre-vocalic", "post-vocalic", "syllabic"
    std::string position = "any";

    // "any", "stressed", "unstressed", "next-unstressed", "prev-stressed"
    std::string stress = "any";

    std::vector<std::u32string> after;      // prev phoneme key filter
    std::vector<std::u32string> before;     // next phoneme key filter

    // Neighbor flag filters (check flags on prev/next phoneme).
    // These complement the key-based after/before filters above.
    std::vector<std::string> afterFlags;    // prev phoneme must have ALL listed flags
    std::vector<std::string> notAfterFlags; // exclude if prev phoneme has ANY listed flag
    std::vector<std::string> beforeFlags;   // next phoneme must have ALL listed flags
    std::vector<std::string> notBeforeFlags;// exclude if next phoneme has ANY listed flag

    // ── Action ────────────────────────────────────────────────────────
    // "replace", "scale", "shift", "insert-before", "insert-after"
    std::string action;

    // ── Action parameters ─────────────────────────────────────────────

    // For "replace":
    std::u32string replaceTo;
    double replaceDurationMs = 0.0;
    bool replaceRemovesClosure = false;
    bool replaceRemovesAspiration = false;
    double replaceClosureScale = 0.0;      // >0: scale closure duration instead of removing
    double replaceAspirationScale = 0.0;   // >0: scale aspiration duration + inject breathiness

    // For "scale":
    std::vector<std::pair<std::string, double>> fieldScales;
    double durationScale = 1.0;
    double fadeScale = 1.0;

    // For "shift":
    struct ShiftEntry {
        std::string field;
        double deltaHz = 0.0;
        double targetHz = 0.0;
        double blend = 1.0;
    };
    std::vector<ShiftEntry> fieldShifts;

    // For "insert-before" / "insert-after":
    std::u32string insertPhoneme;
    double insertDurationMs = 18.0;
    double insertFadeMs = 3.0;
    std::vector<std::string> insertContexts;
};

struct LanguagePack {
  std::string langTag; // normalized (lowercase, '-')

  // Settings / knobs.
  double primaryStressDiv = 1.4;
  double secondaryStressDiv = 1.1;
  
  // ============================================================================
  // Phoneme timing constants (ms at speed=1.0; divided by current speed)
  // ============================================================================
  // These control the base duration and fade times for different phoneme classes.
  // Previously hardcoded in calculateTimes(); now configurable via YAML.
  
  double defaultVowelDurationMs = 60.0;       // Base vowel duration
  double defaultFadeMs = 10.0;                // Default crossfade between segments
  double postStopAspirationDurationMs = 20.0; // Inserted aspiration after voiceless stops
  
  // Consonant durations
  double stopDurationMs = 6.0;                // Plosive burst duration (p, t, k, b, d, g)
  double affricateDurationMs = 24.0;          // Affricate duration (t͡ʃ, d͡ʒ)
  double voicelessFricativeDurationMs = 45.0; // Voiceless fricatives (f, s, ʃ, x)
  double voicedConsonantDurationMs = 30.0;    // Voiced non-vowel default
  double tapDurationMs = 14.0;                // Tap/flap duration (ɾ)
  double trillFallbackDurationMs = 40.0;      // Trill duration if trillModulationMs unset
  double nasalMinDurationMs = 18.0;           // Universal nasal floor (place perception)

  // Vowel context-dependent durations
  double tiedVowelDurationMs = 40.0;          // Vowel tied to following segment (diphthong start)
  double tiedFromVowelDurationMs = 20.0;      // Vowel tied from previous (diphthong end)
  double tiedFromVowelFadeMs = 20.0;          // Fade for tied-from vowels
  double vowelBeforeLiquidDurationMs = 30.0;  // Unstressed vowel before liquid
  double vowelBeforeNasalDurationMs = 40.0;   // Unstressed vowel before nasal
  
  // Transition fades (into vowels)
  double fadeAfterLiquidMs = 25.0;            // Fade into vowel after liquid/semivowel
  double liquidFadeMs = 20.0;                 // Fade for liquid/semivowel segments
  
  // Voice profile name (optional).
  // If set, this profile will be applied to phoneme parameters to produce
  // different voice qualities (e.g., "female" for a female voice).
  // Empty string means no voice profile (use base phoneme parameters).
  std::string voiceProfileName;
  
  // Legacy pitch mode.
  //
  // When enabled, the frontend uses the older time-based pitch curve math
  // from the ee80f4d-era ipa.py (sometimes referred to as ipa-older.py).
  // This tends to sound more like classic screen-reader prosody at higher
  // rates than the newer table-based intonation model.
  //
  // Values:
  //   "espeak_style" (default, same as false) - ToBI-based intonation regions
  //   "legacy" (same as true) - older time-based pitch curve
  //   "fujisaki_style" - Eloquence-style flat base + DSP phrase/accent contours
  std::string legacyPitchMode = "espeak_style";

  // Optional scaling applied to the caller-provided inflection (0..1) when
  // legacyPitchMode is "legacy".
  //
  // Historical NVSpeechPlayer defaults used a lower inflection setting (e.g. 35)
  // than modern defaults (e.g. 60). Feeding those higher values into the legacy
  // math can sound overly excited.
  //
  // 1.0 preserves the historical behavior exactly.
  // A value around 0.58 maps 0.60 -> 0.35.
  double legacyPitchInflectionScale = 0.58;

  // Fujisaki pitch model parameters (used when legacyPitchMode = "fujisaki_style").
  //
  // These control the DSP-level phrase + accent commands in a Fujisaki-Bartman
  // style model (i.e., we keep per-phoneme pitch targets flat and let the DSP
  // synthesize the contour).
  //
  // The defaults aim for a classic screen-reader feel (Eloquence-ish) while
  // avoiding a fully "dead flat" contour.
  double fujisakiPhraseAmp = 0.24;            // Phrase declination arc (log-F0 domain)
  double fujisakiPrimaryAccentAmp = 0.24;     // Primary-stress accent (log-F0 domain)
  double fujisakiSecondaryAccentAmp = 0.12;   // Secondary-stress accent
  
  // Controls which syllables get accent commands:
  //   "all" = every stressed syllable (can get singsongy if amps are too high)
  //   "first_only" = only first primary stress per utterance (very flat)
  //   "off" = no accents, just phrase declination (monotone)
  std::string fujisakiAccentMode = "all";

  // Fujisaki timing parameters (samples @ 22050 Hz). 0 = use DSP default.
  // Lower values = faster/punchier pitch movements (Eloquence-like).
  // DSP defaults: phraseLen=4250 (~193ms), accentLen=1024 (~46ms), accentDur=7500 (~340ms)
  double fujisakiPhraseLen = 0.0;    // Phrase filter rise time (samples)
  double fujisakiAccentLen = 0.0;    // Accent filter attack time (samples)
  double fujisakiAccentDur = 0.0;    // Accent pulse duration (samples)
  double fujisakiDeclinationRate = 0.0003;  // Exponential declination steepness (higher = steeper fall)

  // DEPRECATED: These settings are unused by the exponential declination
  // implementation. Kept for YAML backward compatibility.
  double fujisakiPhraseDecay = 0.75;
  double fujisakiDeclinationScale = 25.0;
  double fujisakiDeclinationMax = 1.25;
  double fujisakiDeclinationPostFloor = 0.15;

  bool postStopAspirationEnabled = false;
  std::u32string postStopAspirationPhoneme = U"h";

  // Stop closure insertion.
  // "always", "vowel-and-cluster", "after-vowel", "none"
  std::string stopClosureMode = "vowel-and-cluster";
  bool stopClosureClusterGapsEnabled = true;

  // If true, allow inserting short closure gaps before stops even after nasals.
  // This helps keep stops audible in nasal+stop clusters (e.g. Hungarian "pont" -> n+t).
  // Default false to avoid "clicks" in languages where this sounds unnatural.
  bool stopClosureAfterNasalsEnabled = false;

  // Stop closure timing (ms at speed=1.0; divided by current speed).
  // These control the duration/fade of inserted silence frames (preStopGap)
  // before stops/affricates.
  double stopClosureVowelGapMs = 41.0;
  double stopClosureVowelFadeMs = 10.0;
  double stopClosureClusterGapMs = 22.0;
  double stopClosureClusterFadeMs = 4.0;

  // Optional: if >0, override cluster gap timing specifically at word boundaries
  // (when the stop/affricate is at word start).
  double stopClosureWordBoundaryClusterGapMs = 0.0;
  double stopClosureWordBoundaryClusterFadeMs = 0.0;

  // Segment boundary timing (ms at speed=1.0; divided by current speed).
  // If non-zero, the frontend inserts a tiny silence frame between consecutive
  // nvspFrontend_queueIPA calls on the same handle. This helps when callers
  // stitch UI speech from multiple chunks (label / role / value), where
  // transitions can otherwise feel abrupt in some languages.
  double segmentBoundaryGapMs = 0.0;
  double segmentBoundaryFadeMs = 0.0;
  // If true (default), suppress the segmentBoundary* silence insertion when the
  // previous chunk ends with a vowel/semivowel and the next chunk starts with a
  // vowel/semivowel. This avoids audible "holes" in vowel-to-vowel transitions
  // (e.g. diphthongs across chunks).
  bool segmentBoundarySkipVowelToVowel = true;

  // If true, also suppress the segmentBoundary* silence insertion when the
  // previous chunk ends with a vowel/semivowel and the next chunk starts with a
  // liquid-like consonant (liquids/taps/trills).
  //
  // This helps avoid audible seams in cases like vowel+R transitions across
  // chunks (e.g. "play" + "er"), where a tiny boundary pause can sound like an
  // unnatural syllable break.
  //
  // Disabled by default to preserve existing behavior.
  bool segmentBoundarySkipVowelToLiquid = false;

  // Single-word utterance tuning (key echo / word-by-word reading).
  //
  // The NVDA driver often speaks "echo" items (individual letters/words) as
  // isolated one-word utterances. The Klatt engine needs an explicit NULL
  // frame to fade to silence; without it, the last voiced phoneme can sound
  // clipped (especially on /r/ and /uː/ in "R" / "are" / "you").

  // Enable the single-word tuning block below.
  bool singleWordTuningEnabled = false;

  // Extra hold added to the final voiced vowel/liquid/nasal (ms at speed=1.0).
  // This is in addition to any prosody / phrase lengthening.
  double singleWordFinalHoldMs = 0.0;

  // Scale factor for singleWordFinalHoldMs when the final segment is a liquid
  // (like R or L). Liquids can sound unnatural when held too long due to their
  // extreme formant positions. Default 1.0 = no reduction. Use 0.3-0.5 for
  // languages like US English where word-final R sounds "pirate-y" when held.
  double singleWordFinalLiquidHoldScale = 1.0;

  // If >0, append a final silence frame with this fade time (ms at speed=1.0)
  // to avoid abrupt cutoffs at the end of single-word utterances.
  double singleWordFinalFadeMs = 0.0;

  // If >0, append a final silence frame with this fade time at the end of ALL
  // utterances (multi-word included).  This lets clause-final voiceless stops
  // decay their aspiration through the cascade instead of hitting the crude
  // stopFade path.  For single-word utterances, singleWordFinalFadeMs takes
  // precedence if set.
  double clauseFinalFadeMs = 0.0;

  // Optional: override the clause type used for intonation when the utterance
  // is a single word. This is useful when callers pass clauseType=',' to keep
  // phrases sounding "ongoing"; for isolated words this can sound like an
  // odd rising comma. Use 0 to disable.
  char singleWordClauseTypeOverride = 0;

  // If true (default), only apply singleWordClauseTypeOverride when the caller
  // provided clauseType=','.
  bool singleWordClauseTypeOverrideCommaOnly = true;

  // Automatic diphthong handling (optional).
  //
  // eSpeak usually encodes diphthongs as a sequence of two vowel qualities,
  // sometimes with a tie bar (U+0361). If tie bars are absent, the frontend
  // can optionally treat some vowel+vowel sequences as a diphthong by marking
  // them as tied internally. This makes the second vowel behave like a short
  // offglide without requiring a dedicated diphthong phoneme in phonemes.yaml.
  //
  // This is off by default to avoid changing languages that rely on vowel
  // hiatus (two syllables). Enable per-language in packs if desired.
  bool autoTieDiphthongs = false;
  // If true, and autoTieDiphthongs is enabled, try to replace the offglide
  // vowel with a semivowel (e.g. i/ɪ -> j, u/ʊ -> w) when those phonemes exist
  // in phonemes.yaml. This can make diphthong movement more obvious.
  bool autoDiphthongOffglideToSemivowel = false;

  // Semivowel offglide shortening (optional).
  //
  // In some packs (notably English), diphthongs are rendered as vowel+semivowel
  // sequences (e.g. eɪ -> ej). When that offglide semivowel is followed by a
  // vowel or a liquid-like consonant (e.g. "player" /plˈejɚ/, "later" /lˈejɾɚ/),
  // giving the semivowel a full consonant duration can sound like a tiny
  // syllable break.
  //
  // If semivowelOffglideScale != 1.0, the engine multiplies the duration/fade
  // of semivowel tokens that occur *within a word* in the pattern:
  //   vowel + semivowel + (vowel | liquid | tap | trill)
  //
  // Default 1.0 = disabled.
  double semivowelOffglideScale = 1.0;

  // Trill amplitude modulation (optional).
  //
  // Some languages use a true trill for /r/. Formant synthesis can render this
  // more naturally when we modulate the trill's voicing rather than relying on
  // pack-level hacks (e.g. duplicating 'r' tokens).
  //
  // When trillModulationMs > 0, the frontend will render `_isTrill` phonemes as
  // a series of micro-frames that apply an amplitude modulation to voiceAmplitude.
  //
  // - trillModulationMs: base trill duration in milliseconds (at speed=1.0).
  //   This also acts as the enable flag (> 0).
  // - trillModulationFadeMs: fade duration (ms) between micro-frames (0 = auto).
  //
  // NOTE: trillModulationMs is subject to normal speed scaling (like other durations).
  // The internal flutter cycle rate is fixed in code (see ipa_engine.cpp).
  double trillModulationMs = 0.0;
  double trillModulationFadeMs = 0.0;

  // Intra-word vowel hiatus break (optional).
  //
  // If enabled (>0), insert a short silence between two adjacent vowels
  // when the second vowel is explicitly stressed (ˈ or ˌ) and both
  // vowels are within the same word.
  //
  // This mainly helps spelled-out acronyms (e.g. NVDA -> ... iːˈeɪ)
  // where the vowel-to-vowel transition can otherwise sound like an extra glide.
  //
  // Disabled by default.
  double stressedVowelHiatusGapMs = 0.0;
  double stressedVowelHiatusFadeMs = 0.0;

  // Spelling diphthong handling (optional).
  //
  // Some eSpeak IPA outputs for spelled-out acronyms run letter names together
  // (e.g. NVDA -> ˌɛnvˌiːdˌiːˈeɪ). If the letter name contains an explicit
  // diphthong, the vowel movement can sound like an extra glide when it follows
  // another vowel.
  //
  // When spellingDiphthongMode is set to 'monophthong', the frontend will
  // selectively render certain letter-name diphthongs (currently just English
  // letter 'A' /eɪ/) as a long monophthong in acronym-like words, reducing the
  // audible 'y' glide without inserting a pause.
  //
  // Allowed values: 'none' (default), 'monophthong'.
  std::string spellingDiphthongMode = "none";

  // Duration scaling.
  double lengthenedScale = 1.05;

// Optional: phonemic length & gemination constraints (post-timing pass).
// Values are interpreted as milliseconds at speed=1 and internally scaled by ctx.speed.
bool lengthContrastEnabled = false;
double lengthContrastShortVowelCeilingMs = 80.0;
double lengthContrastLongVowelFloorMs = 120.0;
double lengthContrastGeminateClosureScale = 1.8;
double lengthContrastGeminateReleaseScale = 0.9;
double lengthContrastPreGeminateVowelScale = 0.85;

  double lengthenedScaleHu = 1.3;
  bool applyLengthenedScaleToVowelsOnly = true;

  // Optional: additional scaling for lengthened vowels (ː) when they occur in a
  // final closed syllable.
  //
  // Specifically, this multiplier is applied when:
  //   - the current token is a vowel with a length mark (ː)
  //   - the vowel is followed (within the same word) by one or more consonants
  //   - there are no later vowel nuclei before the next word boundary
  //
  // This gives packs a clean way to slightly shorten long vowels before
  // word-final consonant clusters (e.g. "rules" /ruːlz/) without affecting
  // open-syllable cases like "ruler" /ruːlə/.
  //
  // Default 1.0 = disabled.
  double lengthenedVowelFinalCodaScale = 1.0;

  // ---------------------------------------------------------------------------
  // Frontend token-level rule passes
  //
  // These are implemented in C++ (src/frontend/passes/*) and can be enabled
  // per-language via lang.yaml settings. All options have safe defaults so that
  // older packs continue to load.
  // ---------------------------------------------------------------------------

  // Coarticulation (locus targets + optional velar pinch).
  //
  // The goal is not to "invent" new phonemes, but to nudge stop consonants
  // toward more speech-like formant transitions.
  //
  // Defaults are conservative.
  bool coarticulationEnabled = true;
  double coarticulationStrength = 0.25;          // 0..1
  double coarticulationWordInitialFadeScale = 1.0;

  // If true, scale coarticulation strength down when the nearest vowel is not
  // directly adjacent (e.g. consonant clusters). This makes the effect more
  // graded instead of "all-or-nothing".
  bool coarticulationGraduated = true;

  // Maximum number of intervening consonants to look through when estimating
  // vowel adjacency for graduated coarticulation.
  //
  // 0 = only immediate vowel neighbors
  // 1 = allow one consonant in between (CCV/VC C)
  // 2+ = allow deeper clusters
  //
  // Stored as a double for YAML simplicity; treated as an int in code.
  double coarticulationAdjacencyMaxConsonants = 2.0;

  double coarticulationLabialF2Locus = 800.0;
  double coarticulationAlveolarF2Locus = 1800.0;
  // Velar F2 locus is typically mid (contextualized further by velar pinch).
  double coarticulationVelarF2Locus = 1200.0;

  // MITalk-style locus interpolation weight (k).
  // locus = src + k * (trg - src)
  // A value around 0.42 is commonly used and keeps the locus closer to the
  // consonant while still reflecting vowel context.
  double coarticulationMitalkK = 0.42;

  // Per-formant scaling on top of coarticulationStrength.
  // F2 carries most of the perceptual load; F1/F3 are typically kept gentler.
  double coarticulationF1Scale = 0.6;
  double coarticulationF2Scale = 1.0;
  double coarticulationF3Scale = 0.5;

  // Per-place-of-articulation strength multiplier.  Multiplies the effective
  // coarticulation strength for consonants at that place.  Labials default
  // lower because lip rounding is relatively independent of tongue body —
  // full-strength labial coarticulation drags F2 so low it sounds like /w/.
  double coarticulationLabialScale = 0.5;
  double coarticulationAlveolarScale = 1.0;
  double coarticulationPalatalScale = 1.0;
  double coarticulationVelarScale = 1.0;

  // Aspiration coarticulation: shape post-stop aspiration formants along
  // the stop→vowel locus trajectory instead of leaving generic /h/ values.
  // BlendStart controls the aspiration's initial formants (0 = stop locus,
  // 1 = vowel target).  BlendEnd controls the aspiration's end formants.
  // The DSP ramps from start to end, creating a smooth transition.
  double coarticulationAspirationBlendStart = 0.3;
  double coarticulationAspirationBlendEnd = 0.7;

  bool coarticulationVelarPinchEnabled = true;
  double coarticulationVelarPinchThreshold = 1800.0;
  double coarticulationVelarPinchF2Scale = 0.9;
  double coarticulationVelarPinchF3 = 2400.0;

  // Cross-syllable coarticulation reduction.  Multiplies effective
  // strength when consonant and vowel are in different syllables.
  // 1.0 = no reduction, 0.7 = 30% weaker across syllable boundaries.
  double coarticulationCrossSyllableScale = 0.70;

  // Legal syllable onsets for onset maximization.
  // Each entry is an IPA string (e.g. "stɹ", "pl", "bɹ").
  // Used by text_parser to place syllable boundaries correctly.
  // Empty = onset maximization disabled for this language.
  std::vector<std::u32string> legalOnsets;

  // Special coarticulation rules (language-specific Hz deltas).
  bool specialCoarticulationEnabled = false;
  std::vector<SpecialCoarticRule> specialCoarticRules;
  double specialCoarticMaxDeltaHz = 400.0;

  // Cluster timing — context-sensitive consonant duration adjustment.
  bool clusterTimingEnabled = false;
  double clusterTimingFricBeforeStopScale = 0.65;
  double clusterTimingStopBeforeFricScale = 0.70;
  double clusterTimingFricBeforeFricScale = 0.75;
  double clusterTimingStopBeforeStopScale = 0.60;
  double clusterTimingTripleClusterMiddleScale = 0.55;
  double clusterTimingWordMedialConsonantScale = 0.85;
  double clusterTimingWordFinalObstruentScale = 0.90;
  double clusterTimingAffricateInClusterScale = 0.75;

  // Syllable-aware duration — onset/coda asymmetry and open-syllable compression.
  bool syllableDurationEnabled = false;
  double syllableDurationOnsetScale = 1.10;                 // onset C's get 10% more time
  double syllableDurationCodaScale = 0.85;                  // coda C's get 15% less time
  double syllableDurationUnstressedOpenNucleusScale = 0.80; // unstressed open-syllable vowels compress

  // Cluster blend — C→C articulatory anticipation (formant ramping).
  //
  // Sets endCf targets on the first consonant of a CC pair so its formants
  // begin moving toward the second consonant's place of articulation.
  // Complements cluster_timing (duration) and boundary_smoothing (fade speed).
  bool   clusterBlendEnabled = false;
  double clusterBlendStrength = 0.35;           // Master blend fraction (0–1)

  // Per-manner-pair scale factors (multiplied by strength).
  double clusterBlendNasalToStopScale   = 1.30; // /ŋk/, /mp/, /nt/ — strongest
  double clusterBlendFricToStopScale    = 0.85; // /st/, /sk/, /sp/
  double clusterBlendStopToFricScale    = 0.70; // /ts/, /ks/
  double clusterBlendNasalToFricScale   = 1.00; // /nf/, /ns/
  double clusterBlendLiquidToStopScale  = 0.85; // /lt/, /rk/
  double clusterBlendLiquidToFricScale  = 0.75; // /ls/, /rf/
  double clusterBlendFricToFricScale    = 0.60; // /sʃ/ (rare)
  double clusterBlendStopToStopScale    = 0.55; // /kt/, /pt/
  double clusterBlendDefaultPairScale   = 0.50; // Everything else

  // Context modifiers.
  double clusterBlendHomorganicScale    = 0.30; // Same place → less spectral shift needed
  double clusterBlendWordBoundaryScale  = 0.50; // C2 starts a new word → weaker blend

  // Per-formant scaling (relative to main strength).
  double clusterBlendF1Scale = 0.50;            // F1 blend is gentler (jaw, not place)
  double clusterBlendForwardDriftStrength = 0.0; // Fill endCf on any token still missing it

  // Boundary crossfade / smoothing (optional).
  //
  // This is a simple way to soften harsh segment joins by increasing the fade
  // time only for certain boundary types.
  bool boundarySmoothingEnabled = false;

  // Per-formant transition scaling (fraction of fade time).
  // Values < 1.0 make formants arrive BEFORE the amplitude crossfade
  // finishes, preventing "wrong formants at full amplitude" artifacts.
  // F1 fastest (place perception), F2/F3 slightly longer.
  double boundarySmoothingF1Scale = 0.3;   // F1 arrives in 30% of fade
  double boundarySmoothingF2Scale = 0.5;   // F2 arrives in 50% of fade
  double boundarySmoothingF3Scale = 0.5;   // F3 arrives in 50% of fade

  // Per-place-of-articulation transition speed overrides.
  // These replace the global F1/F2/F3 scales when a consonant's
  // place is known.  0.0 = use global default.
  //
  // Physics: heavier articulators = slower transitions.
  //   Lips: light, fast -> F2/F3 can be slower (lips independent of tongue)
  //   Tongue tip: light, precise -> F2/F3 fast
  //   Tongue blade (palatal): medium -> F3 slow (F3 IS the place cue)
  //   Tongue body (velar): heavy -> F2 slow (F2 IS the place cue)

  // Labial (/b/, /p/, /m/, /f/, /v/)
  double boundarySmoothingLabialF1Scale  = 0.25;
  double boundarySmoothingLabialF2Scale  = 0.60;
  double boundarySmoothingLabialF3Scale  = 0.55;

  // Alveolar (/t/, /d/, /n/, /s/, /z/, /l/)
  double boundarySmoothingAlveolarF1Scale = 0.30;
  double boundarySmoothingAlveolarF2Scale = 0.40;
  double boundarySmoothingAlveolarF3Scale = 0.35;

  // Palatal / Postalveolar (/ʃ/, /ʒ/, /tʃ/, /dʒ/, /j/)
  double boundarySmoothingPalatalF1Scale = 0.30;
  double boundarySmoothingPalatalF2Scale = 0.55;
  double boundarySmoothingPalatalF3Scale = 0.70;

  // Velar (/k/, /ɡ/, /ŋ/)
  double boundarySmoothingVelarF1Scale   = 0.30;
  double boundarySmoothingVelarF2Scale   = 0.65;
  double boundarySmoothingVelarF3Scale   = 0.60;

  // Syllable-aware transition controls.
  // withinSyllableScale > 1.0 makes within-syllable formant transitions
  // slower (gentler).  1.0 = no effect.  1.5 = 50% slower.
  double boundarySmoothingWithinSyllableScale = 1.5;
  // Within-syllable fade duration multiplier.  Makes crossfade longer
  // for transitions that are part of one articulatory gesture.
  double boundarySmoothingWithinSyllableFadeScale = 1.3;

  // Per-boundary-type fade times (ms, before speed division).
  double boundarySmoothingVowelToStopMs = 22.0;
  double boundarySmoothingStopToVowelMs = 20.0;
  double boundarySmoothingVowelToFricMs = 18.0;
  double boundarySmoothingFricToVowelMs = 18.0;
  double boundarySmoothingVowelToNasalMs = 16.0;
  double boundarySmoothingNasalToVowelMs = 16.0;
  double boundarySmoothingVowelToLiquidMs = 14.0;
  double boundarySmoothingLiquidToVowelMs = 14.0;
  double boundarySmoothingNasalToStopMs = 12.0;
  double boundarySmoothingLiquidToStopMs = 12.0;
  double boundarySmoothingFricToStopMs = 10.0;
  double boundarySmoothingStopToFricMs = 14.0;
  double boundarySmoothingVowelToVowelMs = 18.0;

  // Plosive/nasal-specific transition behavior (universal defaults).
  bool boundarySmoothingPlosiveSpansPhone = true;    // formants ramp across entire plosive
  bool boundarySmoothingNasalF1Instant = true;       // F1 jumps instantly in nasals
  bool boundarySmoothingNasalF2F3SpansPhone = true;  // F2/F3 ramp across entire nasal

  // Trajectory limiting (optional).
  //
  // Caps how quickly selected formant targets are allowed to change at token
  // boundaries by increasing the incoming token's fade (crossfade) time.
  //
  // - applyMask selects which fields are limited (bits by FieldId index).
  // - maxHzPerMs gives per-field max slope.
  // - windowMs caps how far we will extend a boundary fade (ms at speed=1.0).
  bool trajectoryLimitEnabled = false;
  std::uint64_t trajectoryLimitApplyMask =
      (1ULL << static_cast<int>(FieldId::cf2)) |
      (1ULL << static_cast<int>(FieldId::cf3));
  std::array<double, kFrameFieldCount> trajectoryLimitMaxHzPerMs = [] {
    std::array<double, kFrameFieldCount> a{};
    a[static_cast<int>(FieldId::cf2)] = 18.0;
    a[static_cast<int>(FieldId::cf3)] = 22.0;
    return a;
  }();
  double trajectoryLimitWindowMs = 25.0;
  bool trajectoryLimitApplyAcrossWordBoundary = false;
  // Liquids get gentler rate limiting (larger transitions are expected).
  // 1.5 = allow 50% faster Hz/ms than the normal limit.
  double trajectoryLimitLiquidRateScale = 1.5;

  

// Liquid / glide internal dynamics (optional).
// Implemented by splitting the token into a short onglide segment + a steady segment.
bool liquidDynamicsEnabled = false;

// /l/ onglide: quick tongue-tip gesture at the start of /l/ (helps clarity).
double liquidDynamicsLateralOnglideF1Delta = -50.0;
double liquidDynamicsLateralOnglideF2Delta = 200.0;
double liquidDynamicsLateralOnglideDurationPct = 0.30;

// /r/ F3 dip: American English rhotic quality (bunched/retroflex style).
bool liquidDynamicsRhoticF3DipEnabled = false;
double liquidDynamicsRhoticF3Minimum = 1600.0;
double liquidDynamicsRhoticF3DipDurationPct = 0.50;

// /w/ formant movement: start low (labial), then move toward the vowel.
bool liquidDynamicsLabialGlideTransitionEnabled = false;
double liquidDynamicsLabialGlideStartF1 = 300.0;
double liquidDynamicsLabialGlideStartF2 = 700.0;
double liquidDynamicsLabialGlideTransitionPct = 0.60;
// Phrase-final lengthening.
  bool phraseFinalLengtheningEnabled = false;
  double phraseFinalLengtheningFinalSyllableScale = 1.4;
  double phraseFinalLengtheningPenultimateSyllableScale = 1.15;
  double phraseFinalLengtheningStatementScale = 1.0;
  double phraseFinalLengtheningQuestionScale = 0.9;
  bool phraseFinalLengtheningNucleusOnlyMode = true;
  double phraseFinalLengtheningNucleusScale = 0.0;  // 0 = use finalSyllableScale
  double phraseFinalLengtheningCodaScale = 0.0;     // 0 = use finalSyllableScale
  double phraseFinalLengtheningCodaStopScale = 0.0;      // 0 = fall back to codaScale
  double phraseFinalLengtheningCodaFricativeScale = 0.0;  // 0 = fall back to codaScale

  // ── Prominence pass ──
  bool prominenceEnabled = false;

  // Score: phonological stress classification.
  // secondaryStressLevel is the prominence SCORE for secondary stress (0-1).
  double prominenceSecondaryStressLevel = 0.6;
  double prominenceLongVowelWeight = 0.5;
  std::string prominenceLongVowelMode = "unstressed-only"; // "unstressed-only", "always", "never"
  double prominenceWordInitialBoost = 0.0;
  double prominenceWordFinalReduction = 0.0;

  // Realization: direct duration multipliers (like old stressDiv but vowel-only).
  // 1.4 = 40% longer, 1.0 = no change, 0.5 = halved.
  double prominencePrimaryStressWeight = 1.4;
  double prominenceSecondaryStressWeight = 1.1;

  // Realization: duration safety nets
  double prominenceDurationProminentFloorMs = 0.0;
  double prominenceDurationReducedCeiling = 1.0;

  // Realization: amplitude
  double prominenceAmplitudeBoostDb = 0.0;
  double prominenceAmplitudeReductionDb = 0.0;

  // Realization: pitch (controls whether pitch_fujisaki reads prominence)
  bool prominencePitchFromProminence = false;

  // Microprosody.
  bool microprosodyEnabled = false;
  bool microprosodyVoicelessF0RaiseEnabled = true;
  double microprosodyVoicelessF0RaiseHz = 15.0;
  double microprosodyVoicelessF0RaiseEndHz = 0.0;  // 0 = full decay
  bool microprosodyVoicedF0LowerEnabled = true;
  double microprosodyVoicedF0LowerHz = 8.0;
  double microprosodyMinVowelMs = 25.0;

  // Following-consonant F0: adjust vowel END pitch based on next C.
  bool microprosodyFollowingF0Enabled = true;
  double microprosodyFollowingVoicelessRaiseHz = 10.0;
  double microprosodyFollowingVoicedLowerHz = 5.0;

  // Voiced fricatives lower F0 less than voiced stops.
  double microprosodyVoicedFricativeLowerScale = 0.6;

  // Intrinsic vowel F0: high vowels slightly higher, low vowels lower.
  bool microprosodyIntrinsicF0Enabled = true;
  double microprosodyIntrinsicF0HighThreshold = 400.0;  // F1 below = high vowel
  double microprosodyIntrinsicF0LowThreshold = 600.0;   // F1 above = low vowel
  double microprosodyIntrinsicF0HighRaiseHz = 6.0;
  double microprosodyIntrinsicF0LowDropHz = 4.0;

  // Pre-voiceless shortening: vowels before voiceless C are shorter.
  bool microprosodyPreVoicelessShortenEnabled = true;
  double microprosodyPreVoicelessShortenScale = 0.85;  // 85% of normal
  double microprosodyPreVoicelessMinMs = 25.0;         // matches rate comp floor
  double microprosodyMaxTotalDeltaHz = 0.0;            // 0 = no cap

  // ── Rate compensation ──
  // Enforces perceptual duration floors at high speech rates.
  // Prevents speed compression from pushing segments below audibility.
  // Runs PostTiming after cluster_timing, cluster_blend, and prominence.
  bool rateCompEnabled = false;

  // Per-class minimum durations (ms). Absolute floors regardless of speed.
  // Set to 0.0 to disable floor for that class.
  double rateCompVowelFloorMs = 25.0;
  double rateCompFricativeFloorMs = 18.0;
  double rateCompStopFloorMs = 4.0;
  double rateCompNasalFloorMs = 18.0;
  double rateCompLiquidFloorMs = 15.0;
  double rateCompAffricateFloorMs = 12.0;
  double rateCompSemivowelFloorMs = 10.0;
  double rateCompTapFloorMs = 4.0;
  double rateCompTrillFloorMs = 12.0;
  double rateCompVoicedConsonantFloorMs = 10.0;

  // Word-final segments get extra floor padding (added to class floor).
  double rateCompWordFinalBonusMs = 5.0;

  // Optional: let floors shrink slightly at extreme speeds (0.0 = rigid).
  double rateCompFloorSpeedScale = 0.0;

  // Cluster proportion guard: prevent single-segment bulge after floor
  // enforcement. If one consonant in a cluster hits its floor, nudge
  // neighbors proportionally.
  bool rateCompClusterProportionGuard = true;
  double rateCompClusterMaxRatioShift = 0.4;

  // Absorbed from old reduction pass: rate-dependent schwa shortening.
  // At speeds above threshold, unstressed schwas shorten. Floor still
  // enforced — this can't create sub-threshold segments.
  bool rateCompSchwaReductionEnabled = false;
  double rateCompSchwaThreshold = 2.5;
  double rateCompSchwaScale = 0.8;

  // Word-final schwa reduction.
  // Languages like Danish, German, French, European Portuguese reduce word-final
  // schwas heavily. This applies a duration scale to unstressed word-final schwas.
  bool wordFinalSchwaReductionEnabled = false;
  double wordFinalSchwaScale = 0.6;            // 0.0-1.0, lower = shorter
  double wordFinalSchwaMinDurationMs = 8.0;    // floor to avoid total silence

  // Anticipatory nasalization.
  bool nasalizationAnticipatoryEnabled = false;
  double nasalizationAnticipatoryAmplitude = 0.4;
  double nasalizationAnticipatoryBlend = 0.5;

  // Data-driven allophone rules (replaces all old positionalAllophones* settings).
  bool allophoneRulesEnabled = false;
  std::vector<AllophoneRule> allophoneRules;

  // Language-specific duration tweaks.
  bool huShortAVowelEnabled = true;
  std::u32string huShortAVowelKey = U"ᴒ";
  double huShortAVowelScale = 0.85;

  bool englishLongUShortenEnabled = true;
  std::u32string englishLongUKey = U"u";
  double englishLongUWordFinalScale = 0.80;

  // Output frame defaults.
  double defaultPreFormantGain = 1.0;
  double defaultOutputGain = 1.5;

  // Non-acoustic voice defaults (safe to keep at 0, but configurable).
  double defaultVibratoPitchOffset = 0.0;
  double defaultVibratoSpeed = 0.0;
  double defaultVoiceTurbulenceAmplitude = 0.0;
  double defaultGlottalOpenQuotient = 0.0;

  // Normalization pipeline.
  bool stripAllophoneDigits = true;
  bool stripHyphen = true;

  std::unordered_map<std::u32string, std::u32string> aliases;
  std::vector<ReplacementRule> preReplacements;
  std::vector<ReplacementRule> replacements;
  std::unordered_map<std::string, std::vector<std::u32string>> classes;

  // Transforms applied to phoneme field values after correction.
  std::vector<TransformRule> transforms;

  // Intonation by clause punctuation.
  std::unordered_map<char, IntonationClause> intonation;

  // Tonal support.
  bool tonal = false;
  // Map tone string (e.g. "1", "˥˩") -> contour points (percent values).
  std::unordered_map<std::u32string, std::vector<int>> toneContours;
  bool toneDigitsEnabled = true; // allow 1-5 as tone markers
  // If true: contour points are absolute 0..100 values (same scale as intonation).
  // If false: contour points are treated as offsets from the current syllable baseline.
  bool toneContoursAbsolute = true;
};

struct PackSet {
  std::unordered_map<std::u32string, PhonemeDef> phonemes;
  LanguagePack lang;
  
  // Sorted phoneme keys for greedy longest-match tokenization.
  // Populated by loadPackSet() after phonemes are loaded.
  // Keys are sorted by length descending so longer keys match first.
  std::vector<std::u32string> sortedPhonemeKeys;
  
  // Voice profiles (optional). Loaded from "voiceProfiles:" in phonemes.yaml.
  // If no profiles are defined, this will be empty/null.
  std::unique_ptr<VoiceProfileSet> voiceProfiles;
  
  // Non-fatal warnings accumulated during pack loading (e.g., voice profile
  // parse errors). Empty if no warnings. Useful for debugging "why does
  // my profile do nothing?" issues.
  std::string loadWarnings;

  // Stress dictionary: word → stress digits (0=unstressed, 1=primary, 2=secondary).
  // Loaded from packs/dict/{langTag}-stress.tsv at pack load time.
  // Empty if no dict file exists for this language (= no-op).
  std::unordered_map<std::string, std::vector<int>> stressDict;
};

// Load phonemes.yaml + merged language packs.
// packDir is the directory that contains "packs".
// Returns true on success.
bool loadPackSet(
  const std::string& packDir,
  const std::string& langTag,
  PackSet& out,
  std::string& outError
);

// Utility: does this pack contain a phoneme key?
bool hasPhoneme(const PackSet& pack, const std::u32string& key);

// Map a frame field name (e.g. "cf1") to FieldId. Returns true on success.
bool parseFieldId(const std::string& name, FieldId& out);

} // namespace nvsp_frontend

#endif
