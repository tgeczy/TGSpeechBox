/*
TGSpeechBox — Klatt hat-pattern pitch model pass.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

// =============================================================================
// Klatt Hat-Pattern Pitch Pass — pitch contour generation
// =============================================================================
//
// Implements the Klatt 1987 hat-pattern intonation model.
//
// The hat pattern is a three-state model observed in English declarative
// sentences: pitch starts at a baseline, rises sharply on the first
// primary-stressed syllable (the "hat rise"), sustains a raised plateau
// with per-stress peaks through the nuclear region, then falls back below
// baseline on the final stressed syllable (the "hat fall").
//
// The model applies:
//   1. Linear baseline declination throughout the utterance.
//   2. A step-up (hat rise) on the first primary-stressed vowel.
//   3. Diminishing stress peaks on the hat plateau.
//   4. A clause-type-dependent fall (or rise) on the last stressed vowel.
//   5. Optional glottal lowering on the final vowel for statements.
//   6. Single-pole IIR smoothing to avoid discontinuities.

#include "pitch_klatt.h"
#include "pitch_common.h"
#include "../ipa_engine.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nvsp_frontend {

void applyPitchKlatt(
    std::vector<Token>& tokens,
    const PackSet& pack,
    double speed,
    double basePitch,
    double inflection,
    char clauseType) {

  if (tokens.empty()) return;

  const auto& lang = pack.lang;
  const size_t n = tokens.size();

  // Read pack settings (Klatt 1987 hat-pattern parameters).
  const double hatRiseHz            = lang.klattHatRiseHz;
  const double stress1Hz            = lang.klattStress1Hz;
  const double stress2Hz            = lang.klattStress2Hz;
  const double stress3Hz            = lang.klattStress3Hz;
  const double stress4Hz            = lang.klattStress4Hz;
  const double declinationHzPerSec  = lang.klattDeclinationHzPerSec;
  const double finalFallBelowBaseHz = lang.klattFinalFallBelowBaseHz;
  const double questionRiseHz       = lang.klattQuestionRiseHz;
  const double continuationRiseHz   = lang.klattContinuationRiseHz;
  const double glottalLowerHz       = lang.klattGlottalLowerHz;
  const double smoothAlpha          = lang.klattSmoothAlpha;

  // -------------------------------------------------------------------------
  // Pre-scan: locate first and last primary-stressed vowels, count stresses,
  // and measure total voiced duration for declination.
  // -------------------------------------------------------------------------
  int firstStressIdx = -1;
  int lastStressIdx = -1;
  int totalStressed = 0;
  double totalVoicedDurationMs = 0.0;

  // Pending stress: eSpeak marks stress on the syllable-initial consonant,
  // so we carry it forward to the vowel nucleus.
  int pendingStress = 0;

  for (size_t i = 0; i < n; ++i) {
    const Token& t = tokens[i];

    if (t.syllableStart) {
      pendingStress = t.stress;
    }

    if (t.silence || !t.def) continue;

    if (!t.silence && t.def) {
      totalVoicedDurationMs += t.durationMs;
    }

    if (tokenIsVowel(t) && pendingStress == 1) {
      if (firstStressIdx < 0) firstStressIdx = static_cast<int>(i);
      lastStressIdx = static_cast<int>(i);
      ++totalStressed;
      pendingStress = 0;  // consumed
    }
  }

  // Fallback: if no primary-stressed vowel found, treat the first vowel
  // as both first and last stress.
  if (firstStressIdx < 0) {
    for (size_t i = 0; i < n; ++i) {
      if (tokenIsVowel(tokens[i])) {
        firstStressIdx = static_cast<int>(i);
        lastStressIdx = static_cast<int>(i);
        totalStressed = 1;
        break;
      }
    }
  }

  // If there are truly no vowels at all, just assign flat basePitch.
  if (firstStressIdx < 0) {
    for (size_t i = 0; i < n; ++i) {
      Token& t = tokens[i];
      if (t.silence || !t.def) continue;
      setPitchFields(t, basePitch, basePitch);
    }
    return;
  }

  // -------------------------------------------------------------------------
  // State machine: compute raw pitch targets per token
  // -------------------------------------------------------------------------
  enum class HatState { BEFORE_HAT, ON_HAT, AFTER_HAT };

  // Raw pitch arrays (start and end per token). We fill these first, then
  // smooth.
  std::vector<double> rawStart(n, basePitch);
  std::vector<double> rawEnd(n, basePitch);

  HatState state = HatState::BEFORE_HAT;
  double elapsedMs = 0.0;
  double hatLevel = 0.0;         // Hz above baseline when on hat
  int stressIndex = 0;           // Which stressed vowel we're on (0-based)
  double postFallPitch = 0.0;    // Pitch level after the hat fall
  int lastVowelIdx = -1;         // Track last vowel for glottal lowering

  // Reset pending stress tracker for the main pass.
  pendingStress = 0;

  // Find the last vowel (stressed or not) for glottal lowering.
  for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
    if (tokenIsVowel(tokens[static_cast<size_t>(i)])) {
      lastVowelIdx = i;
      break;
    }
  }

  for (size_t i = 0; i < n; ++i) {
    Token& t = tokens[i];

    if (t.syllableStart) {
      pendingStress = t.stress;
    }

    if (t.silence || !t.def) continue;

    // Baseline with linear declination.
    double baseline = basePitch -
        (declinationHzPerSec * elapsedMs / 1000.0 * inflection * speed);
    double baselineEnd = basePitch -
        (declinationHzPerSec * (elapsedMs + t.durationMs) / 1000.0 * inflection * speed);

    // Check if this is a primary-stressed vowel.
    bool isPrimaryStressedVowel =
        (tokenIsVowel(t) && pendingStress == 1);

    // State transitions.
    if (state == HatState::BEFORE_HAT &&
        static_cast<int>(i) == firstStressIdx) {
      // Rise onto the hat.
      hatLevel = hatRiseHz * inflection;
      state = HatState::ON_HAT;
    }

    double startPitch = 0.0;
    double endPitch = 0.0;

    switch (state) {
      case HatState::BEFORE_HAT: {
        // Declining baseline before the hat.
        startPitch = baseline;
        endPitch = baselineEnd;
        break;
      }

      case HatState::ON_HAT: {
        // Hat plateau: baseline + hat level.
        startPitch = baseline + hatLevel;
        endPitch = baselineEnd + hatLevel;

        // Stress peaks (diminishing).
        if (isPrimaryStressedVowel) {
          double boost = 0.0;
          switch (stressIndex) {
            case 0: boost = stress1Hz; break;
            case 1: boost = stress2Hz; break;
            case 2: boost = stress3Hz; break;
            default: boost = stress4Hz; break;
          }
          boost *= inflection;
          startPitch += boost;
          endPitch += boost;
          ++stressIndex;
        }

        // Hat fall: transition on the last primary-stressed vowel.
        if (static_cast<int>(i) == lastStressIdx) {
          if (clauseType == '?' ) {
            // Question: rise instead of fall.
            endPitch = baselineEnd + questionRiseHz * inflection;
          } else if (clauseType == ',') {
            // Continuation: moderate rise.
            endPitch = baselineEnd + continuationRiseHz * inflection;
          } else {
            // Statement ('.', '!') or default: fall below baseline.
            endPitch = baselineEnd - finalFallBelowBaseHz * inflection;
          }
          postFallPitch = endPitch;
          state = HatState::AFTER_HAT;
        }

        if (isPrimaryStressedVowel) {
          pendingStress = 0;  // consumed
        }
        break;
      }

      case HatState::AFTER_HAT: {
        // Continue from the post-fall level, with ongoing baseline decline.
        startPitch = postFallPitch;
        endPitch = postFallPitch -
            (declinationHzPerSec * t.durationMs / 1000.0 * inflection * speed);
        postFallPitch = endPitch;
        break;
      }
    }

    // Glottal lowering on the very last vowel for statements/exclamations.
    if (static_cast<int>(i) == lastVowelIdx &&
        (clauseType == '.' || clauseType == '!')) {
      endPitch -= glottalLowerHz * inflection;
    }

    rawStart[i] = startPitch;
    rawEnd[i] = endPitch;

    elapsedMs += t.durationMs;

    // Consume stress even if we didn't use it above (non-vowel token).
    if (isPrimaryStressedVowel) {
      pendingStress = 0;
    }
  }

  // -------------------------------------------------------------------------
  // Single-pole IIR smoothing (forward pass)
  // -------------------------------------------------------------------------
  // Prevents abrupt pitch jumps at state transitions. The smoothing constant
  // (alpha) controls responsiveness: 0 = no change, 1 = no smoothing.
  const double alpha = std::max(0.0, std::min(1.0, smoothAlpha));

  // Find the first non-silent token to seed the smoother.
  double stateStart = basePitch;
  double stateEnd = basePitch;
  for (size_t i = 0; i < n; ++i) {
    if (!tokens[i].silence && tokens[i].def) {
      stateStart = rawStart[i];
      stateEnd = rawEnd[i];
      break;
    }
  }

  double lastPitch = basePitch;  // Carry-forward for unvoiced/silent tokens.

  for (size_t i = 0; i < n; ++i) {
    Token& t = tokens[i];

    if (t.silence || !t.def) {
      // Unvoiced/silent: carry last pitch forward.
      setPitchFields(t, lastPitch, lastPitch);
      continue;
    }

    // Apply IIR smoothing.
    stateStart += alpha * (rawStart[i] - stateStart);
    stateEnd += alpha * (rawEnd[i] - stateEnd);

    setPitchFields(t, stateStart, stateEnd);
    lastPitch = stateEnd;
  }
}

} // namespace nvsp_frontend
