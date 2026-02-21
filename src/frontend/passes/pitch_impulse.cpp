/*
TGSpeechBox — Impulse pitch model pass.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

// =============================================================================
// Impulse Pitch Pass — pitch contour generation
// =============================================================================
//
// Inspired by the Wintalker speech synthesizer's pitch model.
//
// Architecture:
//   1. Linear declination across the clause (Hz/sec slope).
//   2. Count-based additive stress peaks: first stress gets a large boost,
//      subsequent stresses get progressively smaller boosts.  Each peak
//      decays back to the declining baseline within the vowel.
//   3. Terminal gestures on the final word's last vowel (fall, rise, or
//      continuation rise depending on clause type).
//   4. Two-pole IIR smoothing: two consecutive forward passes with a
//      first-order low-pass filter eliminate pitch discontinuities while
//      preserving the overall contour shape.

#include "pitch_impulse.h"
#include "pitch_common.h"
#include "../ipa_engine.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nvsp_frontend {

void applyPitchImpulse(
    std::vector<Token>& tokens,
    const PackSet& pack,
    double speed,
    double basePitch,
    double inflection,
    char clauseType) {

  if (tokens.empty()) return;

  const auto& lang = pack.lang;

  // -------------------------------------------------------------------------
  // Read settings from pack (these fields will be added to pack.h separately)
  // -------------------------------------------------------------------------
  const double declinRate         = lang.impulseDeclinationHzPerSec;
  const double firstBoost         = lang.impulseFirstStressBoostHz;
  const double secondBoost        = lang.impulseSecondStressBoostHz;
  const double thirdBoost         = lang.impulseThirdStressBoostHz;
  const double fourthBoost        = lang.impulseFourthStressBoostHz;
  const double questionReduction  = lang.impulseQuestionReduction;
  const double terminalFallHz     = lang.impulseTerminalFallHz;
  const double continuationRiseHz = lang.impulseContinuationRiseHz;
  const double questionRiseHz     = lang.impulseQuestionRiseHz;
  const double assertiveness      = lang.impulseAssertiveness;
  const double smoothAlpha        = lang.impulseSmoothAlpha;

  // -------------------------------------------------------------------------
  // Find the last wordStart index (to identify the final word)
  // -------------------------------------------------------------------------
  int lastWordStartIdx = -1;
  for (int i = static_cast<int>(tokens.size()) - 1; i >= 0; --i) {
    if (tokens[static_cast<size_t>(i)].wordStart) {
      lastWordStartIdx = i;
      break;
    }
  }

  // Find the last vowel in the final word (for terminal gesture).
  int finalWordLastVowelIdx = -1;
  if (lastWordStartIdx >= 0) {
    for (int i = static_cast<int>(tokens.size()) - 1; i >= lastWordStartIdx; --i) {
      const Token& t = tokens[static_cast<size_t>(i)];
      if (t.silence || !t.def) continue;
      if (tokenIsVowel(t)) {
        finalWordLastVowelIdx = i;
        break;
      }
    }
  }
  // Fallback: if no vowel found in the final word, scan the whole utterance.
  if (finalWordLastVowelIdx < 0) {
    for (int i = static_cast<int>(tokens.size()) - 1; i >= 0; --i) {
      const Token& t = tokens[static_cast<size_t>(i)];
      if (t.silence || !t.def) continue;
      if (tokenIsVowel(t)) {
        finalWordLastVowelIdx = i;
        break;
      }
    }
  }

  // -------------------------------------------------------------------------
  // First pass: compute raw pitch targets
  // -------------------------------------------------------------------------
  // We store raw startPitch/endPitch in temporary vectors so we can smooth
  // them before writing back to the tokens.
  const size_t n = tokens.size();
  std::vector<double> rawStart(n, basePitch);
  std::vector<double> rawEnd(n, basePitch);
  std::vector<bool>   isPhonetic(n, false);

  double elapsedMs = 0.0;
  int stressIndex = 0;
  double lastPitch = basePitch;  // carry pitch forward for unvoiced tokens

  // eSpeak marks stress on syllable-initial consonants, not vowels.
  // Carry pending stress forward to the vowel nucleus.
  int pendingStress = 0;

  for (size_t i = 0; i < n; ++i) {
    Token& t = tokens[i];

    if (t.syllableStart) {
      pendingStress = t.stress;
    }

    if (t.silence || !t.def) {
      // Silent/undefined: carry forward last computed pitch.
      rawStart[i] = lastPitch;
      rawEnd[i]   = lastPitch;
      continue;
    }

    isPhonetic[i] = true;

    // Linear declination baseline.
    double curBase = basePitch - (declinRate * elapsedMs / 1000.0
                                  * inflection * speed);
    double endBase = basePitch - (declinRate * (elapsedMs + t.durationMs) / 1000.0
                                  * inflection * speed);

    // Clamp baselines so they don't go below half the base pitch.
    const double floor = basePitch * 0.5;
    curBase = std::max(curBase, floor);
    endBase = std::max(endBase, floor);

    elapsedMs += t.durationMs;

    double startPitch = curBase;
    double endPitch   = endBase;

    // ----- Stress peaks -----
    const bool isPrimaryStressedVowel = (tokenIsVowel(t) && pendingStress == 1);
    if (isPrimaryStressedVowel) {
      // Look up boost from the position table.
      double boost = fourthBoost;
      if (stressIndex == 0)      boost = firstBoost;
      else if (stressIndex == 1) boost = secondBoost;
      else if (stressIndex == 2) boost = thirdBoost;

      // Scale boost by inflection.
      boost *= inflection;

      // For questions, reduce stress peaks (prosody flattens toward the rise).
      if (clauseType == '?') {
        boost *= questionReduction;
      }

      // Additive: start at baseline + boost, decay back to baseline by end.
      startPitch = curBase + boost;
      endPitch   = endBase;

      ++stressIndex;
      pendingStress = 0;  // consumed
    }

    // ----- Terminal gesture (final word's last vowel) -----
    if (static_cast<int>(i) == finalWordLastVowelIdx) {
      if (clauseType == '.' || clauseType == '!') {
        // Declarative / exclamatory: pitch falls.
        endPitch -= terminalFallHz * assertiveness * inflection;
      } else if (clauseType == '?') {
        // Question: pitch rises.
        endPitch += questionRiseHz * inflection;
      } else if (clauseType == ',') {
        // Continuation: slight rise.
        endPitch += continuationRiseHz * inflection;
      }
    }

    rawStart[i] = startPitch;
    rawEnd[i]   = endPitch;
    lastPitch   = endPitch;
  }

  // -------------------------------------------------------------------------
  // Two-pole smoothing filter
  // -------------------------------------------------------------------------
  // Two consecutive forward passes with a first-order IIR low-pass filter.
  // Lower alpha = smoother contour; higher alpha = more responsive.
  const double alpha = std::max(0.01, std::min(1.0, smoothAlpha));

  // --- Pass 1: smooth startPitch ---
  {
    // Find first phonetic token to seed the filter.
    double state = basePitch;
    for (size_t i = 0; i < n; ++i) {
      if (isPhonetic[i]) { state = rawStart[i]; break; }
    }
    for (size_t i = 0; i < n; ++i) {
      if (!isPhonetic[i]) continue;
      state += alpha * (rawStart[i] - state);
      rawStart[i] = state;
    }
  }
  // --- Pass 2: smooth startPitch again ---
  {
    double state = basePitch;
    for (size_t i = 0; i < n; ++i) {
      if (isPhonetic[i]) { state = rawStart[i]; break; }
    }
    for (size_t i = 0; i < n; ++i) {
      if (!isPhonetic[i]) continue;
      state += alpha * (rawStart[i] - state);
      rawStart[i] = state;
    }
  }

  // --- Pass 1: smooth endPitch ---
  {
    double state = basePitch;
    for (size_t i = 0; i < n; ++i) {
      if (isPhonetic[i]) { state = rawEnd[i]; break; }
    }
    for (size_t i = 0; i < n; ++i) {
      if (!isPhonetic[i]) continue;
      state += alpha * (rawEnd[i] - state);
      rawEnd[i] = state;
    }
  }
  // --- Pass 2: smooth endPitch again ---
  {
    double state = basePitch;
    for (size_t i = 0; i < n; ++i) {
      if (isPhonetic[i]) { state = rawEnd[i]; break; }
    }
    for (size_t i = 0; i < n; ++i) {
      if (!isPhonetic[i]) continue;
      state += alpha * (rawEnd[i] - state);
      rawEnd[i] = state;
    }
  }

  // -------------------------------------------------------------------------
  // Write smoothed pitch targets back to tokens
  // -------------------------------------------------------------------------
  lastPitch = basePitch;
  for (size_t i = 0; i < n; ++i) {
    Token& t = tokens[i];
    if (t.silence || !t.def) {
      // Carry forward for silent tokens.
      setPitchFields(t, lastPitch, lastPitch);
      continue;
    }
    setPitchFields(t, rawStart[i], rawEnd[i]);
    lastPitch = rawEnd[i];
  }
}

} // namespace nvsp_frontend
