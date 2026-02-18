/*
TGSpeechBox — IPA engine interface and Token definitions.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_IPA_ENGINE_H
#define TGSB_FRONTEND_IPA_ENGINE_H

#include <string>
#include <vector>

#include "pack.h"
#include "nvspFrontend.h"

namespace nvsp_frontend {

// ============================================================================
// Trajectory limiting state (per-handle, NOT static)
// ============================================================================
// This state tracks previous frame formant values for rate-of-change limiting.
// It MUST be stored per-handle (not as function-static variables) to avoid
// data races when multiple engine instances speak concurrently.
//
// The state is reset at the start of each utterance by calling reset().
struct TrajectoryState {
  double prevCf2 = 0.0;
  double prevCf3 = 0.0;
  double prevPf2 = 0.0;
  double prevPf3 = 0.0;
  bool hasPrevFrame = false;
  bool prevWasNasal = false;  // Skip limiting on frames following nasals

  // For equal-power amplitude crossfade detection
  double prevVoiceAmp = 0.0;
  double prevFricAmp = 0.0;

  // Full previous base frame for voice bar emission.
  // Contains pitch, GOQ, outputGain, vibrato — everything PhonemeDef lacks.
  double prevBase[kFrameFieldCount] = {};
  bool hasPrevBase = false;

  void reset() {
    prevCf2 = 0.0;
    prevCf3 = 0.0;
    prevPf2 = 0.0;
    prevPf3 = 0.0;
    hasPrevFrame = false;
    prevWasNasal = false;
    prevVoiceAmp = 0.0;
    prevFricAmp = 0.0;
    hasPrevBase = false;
  }
};

struct Token {
  // If def is null, this token is "silence" (no frame).
  const PhonemeDef* def = nullptr;

  // Per-token field values (only valid where setMask has the bit set).
  std::uint64_t setMask = 0;
  double field[kFrameFieldCount] = {0.0};

  // Meta.
  bool silence = false;
  bool preStopGap = false;
  bool clusterGap = false;
  bool postStopAspiration = false;
  bool vowelHiatusGap = false;
  bool voicedClosure = false;  // Voice bar: maintain low-amplitude voicing during closure

  bool wordStart = false;
  bool syllableStart = false;
  int syllableIndex = -1;   // -1 = unassigned. Set by syllable_marking pass.
  int stress = 0; // 0 none, 1 primary, 2 secondary

  bool tiedTo = false;
  bool tiedFrom = false;
  int lengthened = 0;  // count of length marks (ː) - 0=none, 1=one, 2=double, etc.

  // Base char used for some tweaks (like Hungarian short vowel checks).
  char32_t baseChar = 0;

  // Timing (ms) computed later.
  double durationMs = 0.0;
  double fadeMs = 0.0;

  // Tonal marker captured for this syllable start (UTF-32 string), if any.
  std::u32string tone;
  
  // DECTalk-style formant end targets (set by coarticulation pass)
  // These allow within-phoneme formant ramping for CV transitions.
  // If hasEndCf* is true, the token-level value overrides PhonemeDef.
  bool hasEndCf1 = false;
  bool hasEndCf2 = false;
  bool hasEndCf3 = false;
  double endCf1 = 0.0;
  double endCf2 = 0.0;
  double endCf3 = 0.0;

  // Per-parameter transition speed scales (carried to FrameEx).
  // 0.0 = no override.  See speechPlayer_frameEx_t for semantics.
  double transF1Scale = 0.0;
  double transF2Scale = 0.0;
  double transF3Scale = 0.0;
  double transNasalScale = 0.0;

  // Token-level breathiness override (set by allophone rules).
  // Added to phoneme-level and user-level breathiness in frame_emit.
  bool hasTokenBreathiness = false;
  double tokenBreathiness = 0.0;

  // Fujisaki pitch model markers (set by applyPitchFujisaki)
  // These are passed through to frameEx for DSP-level pitch contour generation.
  bool fujisakiEnabled = false;
  bool fujisakiReset = false;         // Reset model state (at clause start)
  double fujisakiPhraseAmp = 0.0;     // Phrase command amplitude (0 = no command)
  double fujisakiAccentAmp = 0.0;     // Accent command amplitude (0 = no command)

  // Prominence score (0.0–1.0), set by the prominence pass.
  // Used by pitch_fujisaki for accent scaling when prominencePitchFromProminence is true.
  double prominence = -1.0;  // -1.0 = not set (prominence pass didn't run)
};

// ============================================================================
// Token helper functions (inline for use across translation units)
// ============================================================================
inline bool tokenIsTrill(const Token& t) {
  return t.def && ((t.def->flags & kIsTrill) != 0);
}

inline bool tokenIsVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

inline bool tokenIsSemivowel(const Token& t) {
  return t.def && ((t.def->flags & kIsSemivowel) != 0);
}

inline bool tokenIsLiquid(const Token& t) {
  return t.def && ((t.def->flags & kIsLiquid) != 0);
}

// Convert IPA -> tokens.
// This runs:
//  1) normalization (pack rules)
//  2) phoneme mapping
//  3) gap insertion
//  4) copy-adjacent correction
//  5) transforms
//  6) timing + pitch
//
// On success, tokens are ready to be converted to nvspFrontend_Frame.
bool convertIpaToTokens(
  const PackSet& pack,
  const std::string& ipaUtf8,
  double speed,
  double basePitch,
  double inflection,
  char clauseType,
  std::vector<Token>& outTokens,
  std::string& outError
);

// Convert tokens -> callback frames.
// trajectoryState is per-handle state for formant smoothing (must not be null).
void emitFrames(
  const PackSet& pack,
  const std::vector<Token>& tokens,
  int userIndexBase,
  TrajectoryState* trajectoryState,
  nvspFrontend_FrameCallback cb,
  void* userData
);

// Convert tokens -> callback frames with extended parameters (ABI v2+).
// frameExDefaults contains user-level defaults that are mixed with per-phoneme values.
// trajectoryState is per-handle state for formant smoothing (must not be null).
void emitFramesEx(
  const PackSet& pack,
  const std::vector<Token>& tokens,
  int userIndexBase,
  const nvspFrontend_FrameEx& frameExDefaults,
  TrajectoryState* trajectoryState,
  nvspFrontend_FrameExCallback cb,
  void* userData
);

} // namespace nvsp_frontend

#endif
