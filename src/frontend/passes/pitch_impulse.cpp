/*
TGSpeechBox — Impulse pitch model pass.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

// =============================================================================
// Impulse Pitch Pass — pitch contour generation
// =============================================================================
//
// Multi-layer additive pitch model.  Four independent pitch layers are
// summed per token, each contributing a different aspect of natural
// intonation.  This architecture produces lively, speech-like contours
// by letting declination, phrasal hat patterns, lexical stress, and
// terminal gestures interact without fighting each other.
//
// Layer 1 — Proportional declination ramp:
//   Pitch starts elevated (+range/2) and ends depressed (-range/2),
//   distributed proportionally across voiced duration.  This replaces
//   the old fixed Hz/sec slope.
//
// Layer 2 — Hat-pattern rise/fall:
//   Pitch rises by impulseRiseHz at the onset of a word containing a
//   primary-stressed vowel.  It falls by riseHz * hatFallScale at the
//   next word boundary.  Because fall > rise, each cycle contributes
//   a net decline that reinforces the baseline ramp.
//
// Layer 3 — Stress peaks:
//   Additive Hz boost on the stressed vowel nucleus, declining by
//   count position (1st > 2nd > 3rd > 4th+).  All scaled by stressGain.
//   Terminal-position stress is INVERTED (pitch drops instead of rising).
//   Secondary stress gets a reduced peak.
//
// Layer 4 — Terminal gesture:
//   Statement: pitch drops on final vowel.
//   Question: pitch rises across final vowel.
//   Comma: slight continuation rise.
//
// Smoothing: single-pass forward IIR with alpha=0.55 (much lighter than
// the old two-pass alpha=0.3 which crushed the contour).

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
  // Read settings
  // -------------------------------------------------------------------------
  const double declinRangeHz      = lang.impulseDeclinationRangeHz;
  const double declinHzPerSec     = lang.impulseDeclinationHzPerSec;
  const double riseHz             = lang.impulseRiseHz;
  const double hatFallScale       = lang.impulseHatFallScale;
  const double firstBoost         = lang.impulseFirstStressBoostHz;
  const double secondBoost        = lang.impulseSecondStressBoostHz;
  const double thirdBoost         = lang.impulseThirdStressBoostHz;
  const double fourthBoost        = lang.impulseFourthStressBoostHz;
  const double stressGain         = lang.impulseStressGain;
  const double secStressScale     = lang.impulseSecondaryStressScale;
  const double termStressHz       = lang.impulseTerminalStressHz;
  const double questionReduction  = lang.impulseQuestionReduction;
  const double terminalFallHz     = lang.impulseTerminalFallHz;
  const double continuationRiseHz = lang.impulseContinuationRiseHz;
  const double questionRiseHz     = lang.impulseQuestionRiseHz;
  const double assertiveness      = lang.impulseAssertiveness;
  const double smoothAlpha        = std::max(0.01, std::min(1.0,
                                     lang.impulseSmoothAlpha));

  const double callerPitch = basePitch;  // before the exclamation's boost

  // Exclamation register boost: raise baseline pitch for exclamatory speech.
  // Natural exclamations have a higher mean F0, not just bigger peaks.
  if (clauseType == '!') {
    basePitch *= 1.10;
  }

  const size_t n = tokens.size();

  // A question and a comma end going up, the others going down.
  const bool risingClause = (clauseType == '?' || clauseType == ',');

  // =========================================================================
  // Phase 1: Structural scan
  // =========================================================================

  // 1a. Compute total phonetic duration.
  double totalPhoneticMs = 0.0;
  for (const auto& t : tokens) {
    if (!t.silence && t.def) totalPhoneticMs += t.durationMs;
  }

  // 1b. Find word boundaries and which words contain primary/secondary stress.
  // wordStartIndices[w] = token index of word start
  // wordHasStress[w]    = highest stress level in that word (1=primary, 2=secondary)
  std::vector<int> wordStartIndices;
  std::vector<int> wordHasStress;

  int curWordStress = 0;
  int pendingStress = 0;

  for (size_t i = 0; i < n; ++i) {
    const Token& t = tokens[i];

    if (t.wordStart) {
      // Close previous word's stress.
      if (!wordStartIndices.empty()) {
        wordHasStress.push_back(curWordStress);
      }
      wordStartIndices.push_back(static_cast<int>(i));
      curWordStress = 0;
    }

    if (t.syllableStart) {
      pendingStress = t.stress;
    }

    // Check for stress on vowel nuclei.
    if (tokenIsVowel(t) && pendingStress > 0) {
      if (pendingStress == 1) {
        curWordStress = std::max(curWordStress, 1);  // primary wins
      } else if (pendingStress == 2 && curWordStress == 0) {
        curWordStress = 2;  // secondary only if no primary yet
      }
      pendingStress = 0;
    }
  }
  // Close the last word.
  wordHasStress.push_back(curWordStress);

  const int numWords = static_cast<int>(wordStartIndices.size());

  // 1c. Find the terminal word (last word with a vowel) and its last vowel.
  int terminalWordIdx = numWords - 1;
  int finalVowelIdx = -1;
  for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
    const Token& t = tokens[static_cast<size_t>(i)];
    if (t.silence || !t.def) continue;
    if (tokenIsVowel(t)) {
      finalVowelIdx = i;
      break;
    }
  }

  // 1d. Find the last primary-stressed vowel for terminal stress inversion.
  int lastPrimaryVowelIdx = -1;
  {
    int ps = 0;
    for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
      const Token& t = tokens[static_cast<size_t>(i)];
      if (t.syllableStart) ps = t.stress;
      if (tokenIsVowel(t) && ps == 1) {
        lastPrimaryVowelIdx = i;
        break;
      }
    }
  }

  // 1e. Terminal word duration (for progressive terminal gesture).
  double terminalWordDurMs = 0.0;
  int terminalWordStartIdx = 0;
  if (terminalWordIdx >= 0 && terminalWordIdx < numWords) {
    terminalWordStartIdx = wordStartIndices[static_cast<size_t>(terminalWordIdx)];
    // A fall is spread over the last word, a rise belongs to the last
    // syllable: a question's last syllable ends above every earlier peak, a
    // comma's continuation rise is on the last syllable (O'Shaughnessy 1976,
    // PhD thesis, MIT; Allen, Hunnicutt & Klatt 1987, "From Text to Speech:
    // The MITalk System").  Spread over the word and then smoothed, "?" rose
    // 0 to 5 Hz (#155).
    if (risingClause && finalVowelIdx > terminalWordStartIdx)
      terminalWordStartIdx = finalVowelIdx;
    const size_t wEnd = (terminalWordIdx + 1 < numWords)
      ? static_cast<size_t>(wordStartIndices[static_cast<size_t>(terminalWordIdx + 1)])
      : n;
    for (size_t i = static_cast<size_t>(terminalWordStartIdx); i < wEnd; ++i) {
      if (!tokens[i].silence && tokens[i].def)
        terminalWordDurMs += tokens[i].durationMs;
    }
  }

  // =========================================================================
  // Phase 2: Build per-token pitch from 4 layers
  // =========================================================================

  std::vector<double> rawStart(n, basePitch);
  std::vector<double> rawEnd(n, basePitch);
  std::vector<bool>   isPhonetic(n, false);

  double elapsedMs = 0.0;
  double terminalWordElapsedMs = 0.0;  // elapsed phonetic time within terminal word
  double terminalDeclin = 0.0;         // declination held through a rising last syllable
  bool terminalDeclinSet = false;
  int stressCount = 0;
  double hatOffset = 0.0;         // accumulated rise/fall offset
  int currentWordIdx = 0;         // which word we're in
  bool inStressedWord = false;    // current word has primary stress
  pendingStress = 0;

  for (size_t i = 0; i < n; ++i) {
    Token& t = tokens[i];

    // Track word transitions for hat pattern.
    if (t.wordStart && i > 0) {
      // Leaving previous word: if it was stressed, apply fall.
      if (inStressedWord) {
        double fallAmt = riseHz * hatFallScale * inflection * assertiveness;
        if (clauseType == '?') fallAmt *= 0.3;  // less fall in questions
        hatOffset -= fallAmt;
      }
      // Dampen hat accumulation: leaky integrator prevents unbounded
      // pitch decline over long phrases.  Each word boundary decays the
      // offset by 15%, so later words don't keep drilling down.
      hatOffset *= 0.85;
      currentWordIdx++;
    }

    // Entering a new word: check if it contains stress.
    if (t.wordStart && currentWordIdx < numWords) {
      inStressedWord = (wordHasStress[static_cast<size_t>(currentWordIdx)] == 1);

      // Hat rise at onset of stressed words.
      if (inStressedWord) {
        double riseAmt = riseHz * inflection;
        if (clauseType == '?') riseAmt *= questionReduction;
        hatOffset += riseAmt;
      }
    }

    if (t.syllableStart) {
      pendingStress = t.stress;
    }

    if (t.silence || !t.def) {
      rawStart[i] = basePitch + hatOffset;
      rawEnd[i]   = basePitch + hatOffset;
      continue;
    }

    isPhonetic[i] = true;

    // ---- Layer 1: Proportional declination ----
    // Clause-type scaling: questions need much less declination (pitch
    // stays elevated for the terminal rise), commas less than statements.
    // Matches the spirit of Fujisaki clause-type overrides.
    double clauseDeclinScale = 1.0;
    double clauseStressScale = 1.0;   // multiplier on stress boosts
    double clauseTerminalScale = 1.0; // multiplier on terminal fall
    if (clauseType == '?') {
      clauseDeclinScale = 0.25;
    } else if (clauseType == ',') {
      clauseDeclinScale = 0.5;
    } else if (clauseType == '!') {
      clauseDeclinScale = 1.5;   // stronger decline — more emphatic
      clauseStressScale = 1.4;   // bigger stress peaks — exclamatory emphasis
      clauseTerminalScale = 1.5; // stronger final drop
    }

    double declinStart, declinEnd;
    double effectiveRange = declinRangeHz * clauseDeclinScale;
    if (effectiveRange > 0.0 && totalPhoneticMs > 0.0) {
      // Proportional: distribute range across clause.
      double halfRange = (effectiveRange * inflection) / 2.0;
      double progressStart = elapsedMs / totalPhoneticMs;
      double progressEnd = (elapsedMs + t.durationMs) / totalPhoneticMs;
      declinStart = halfRange - (progressStart * effectiveRange * inflection);
      declinEnd   = halfRange - (progressEnd   * effectiveRange * inflection);
    } else {
      // Legacy fixed Hz/sec mode.
      double effectiveHzPerSec = declinHzPerSec * clauseDeclinScale;
      declinStart = -(effectiveHzPerSec * elapsedMs / 1000.0 * inflection * speed);
      declinEnd   = -(effectiveHzPerSec * (elapsedMs + t.durationMs) / 1000.0
                       * inflection * speed);
    }

    elapsedMs += t.durationMs;

    // On a rising last syllable the declination stops, so the rise the pack
    // asks for is the rise that is heard (in a one-word clause the ramp took
    // most of it back).
    if (risingClause && currentWordIdx == terminalWordIdx &&
        static_cast<int>(i) >= terminalWordStartIdx) {
      if (!terminalDeclinSet) {
        terminalDeclin = declinStart;
        terminalDeclinSet = true;
      }
      declinStart = terminalDeclin;
      declinEnd = terminalDeclin;
    }

    // ---- Layer 2: Hat offset (already accumulated above) ----
    // hatOffset is the running accumulation of rises and falls.

    // ---- Layer 3: Stress peaks ----
    double stressPeak = 0.0;
    bool isVowel = tokenIsVowel(t);

    if (isVowel && (pendingStress == 1 || pendingStress == 2)) {
      bool isPrimary = (pendingStress == 1);
      bool isTerminal = (static_cast<int>(i) == lastPrimaryVowelIdx &&
                         (clauseType == '.' || clauseType == '!'));

      if (isTerminal && isPrimary && stressCount > 0) {
        // Terminal primary stress DROPS pitch instead of boosting.
        // Only when prior stresses established a higher register —
        // single-word utterances ("hi.", "b") still get a real peak.
        stressPeak = termStressHz * inflection * assertiveness;
      } else if (isPrimary) {
        // Count-dependent boost.
        double boost = fourthBoost;
        if (stressCount == 0)      boost = firstBoost;
        else if (stressCount == 1) boost = secondBoost;
        else if (stressCount == 2) boost = thirdBoost;

        boost *= stressGain * inflection * clauseStressScale;
        if (clauseType == '?') boost *= questionReduction;

        stressPeak = boost;
        stressCount++;
      } else {
        // Secondary stress: scaled-down primary boost.
        double boost = fourthBoost;
        if (stressCount == 0)      boost = firstBoost;
        else if (stressCount == 1) boost = secondBoost;
        else if (stressCount == 2) boost = thirdBoost;

        boost *= stressGain * secStressScale * inflection * clauseStressScale;
        if (clauseType == '?') boost *= questionReduction;

        stressPeak = boost;
        // Don't increment stressCount for secondary.
      }

      pendingStress = 0;
    }

    // A stress peak sits at the start of its vowel and is gone by the end.
    // On the last vowel of a rising clause that takes the rise back (a comma
    // after a stressed last syllable went 120 to 119 Hz); there the rise is
    // the accent.
    if (risingClause && static_cast<int>(i) == finalVowelIdx) {
      stressPeak = 0.0;
    }

    // ---- Layer 4: Terminal gesture ----
    // Progressive: spread the terminal fall/rise across the entire final
    // word so every phoneme in "button" slopes down naturally, not just
    // the last vowel.  This mirrors WinTalker's per-frame down ramp.
    double terminalOffset = 0.0;
    double terminalEndOffset = 0.0;
    const bool inTerminalSpan = (currentWordIdx == terminalWordIdx &&
                                 static_cast<int>(i) >= terminalWordStartIdx);
    if (inTerminalSpan && terminalWordDurMs > 0.0) {
      double progressStart = terminalWordElapsedMs / terminalWordDurMs;
      double progressEnd = (terminalWordElapsedMs + t.durationMs) / terminalWordDurMs;

      if (clauseType == '.' || clauseType == '!') {
        terminalOffset    = -(terminalFallHz * clauseTerminalScale * assertiveness * inflection * progressStart);
        terminalEndOffset = -(terminalFallHz * clauseTerminalScale * assertiveness * inflection * progressEnd);
      } else if (clauseType == '?') {
        terminalOffset    = questionRiseHz * inflection * progressStart;
        terminalEndOffset = questionRiseHz * inflection * progressEnd;
      } else if (clauseType == ',') {
        terminalOffset    = continuationRiseHz * inflection * progressStart;
        terminalEndOffset = continuationRiseHz * inflection * progressEnd;
      }
    }

    // Track elapsed time within terminal word (after using it for progress).
    if (inTerminalSpan) {
      terminalWordElapsedMs += t.durationMs;
    }

    // ---- Combine all layers ----
    // Stress peak applies fully at start of vowel, decays to baseline by end.
    double startPitch = basePitch + declinStart + hatOffset + stressPeak + terminalOffset;
    double endPitch   = basePitch + declinEnd   + hatOffset + terminalEndOffset;

    // Clamp: don't let pitch drop too far below the base.
    // 0.75 keeps a 100Hz voice above 75Hz (0.5 bottomed at 50Hz).
    const double floor = basePitch * 0.75;
    startPitch = std::max(startPitch, floor);
    endPitch   = std::max(endPitch,   floor);

    rawStart[i] = startPitch;
    rawEnd[i]   = endPitch;
  }

  // If the last word was stressed, apply the final hat fall.
  // (This handles the case where the clause ends on a stressed word.)
  if (inStressedWord) {
    // The terminal gesture already handles final pitch shaping,
    // so we don't add another hat fall on the final vowel.
    // But we do let the accumulated hatOffset carry through.
  }

  // =========================================================================
  // Phase 3: Single-pass forward IIR smoothing
  // =========================================================================
  // Much lighter than the old two-pass: preserves stress peaks and hat contour
  // while smoothing transitions between tokens.

  // The last syllable is left as it is: the smoother only goes part of the
  // way to each target, and the clause-final target is the one that says
  // what kind of clause this was (#155).
  const size_t smoothEnd = (finalVowelIdx >= 0) ? static_cast<size_t>(finalVowelIdx) : n;

  // Smooth startPitch.
  {
    double state = basePitch;
    for (size_t i = 0; i < n; ++i) {
      if (isPhonetic[i]) { state = rawStart[i]; break; }
    }
    for (size_t i = 0; i < smoothEnd; ++i) {
      if (!isPhonetic[i]) continue;
      state += smoothAlpha * (rawStart[i] - state);
      rawStart[i] = state;
    }
  }

  // Smooth endPitch.
  {
    double state = basePitch;
    for (size_t i = 0; i < n; ++i) {
      if (isPhonetic[i]) { state = rawEnd[i]; break; }
    }
    for (size_t i = 0; i < smoothEnd; ++i) {
      if (!isPhonetic[i]) continue;
      state += smoothAlpha * (rawEnd[i] - state);
      rawEnd[i] = state;
    }
  }

  // =========================================================================
  // Phase 4: Write back to tokens
  // =========================================================================
  double lastPitch = basePitch;
  for (size_t i = 0; i < n; ++i) {
    Token& t = tokens[i];
    if (t.silence || !t.def) {
      setPitchFields(t, lastPitch, lastPitch);
      continue;
    }
    setPitchFields(t, rawStart[i], rawEnd[i]);
    // Unsmoothed, the last syllable's movement is steep; on a word-initial
    // vowel the word-boundary dip frame would run all of it in 8 ms and the
    // vowel would start again from the top.
    t.pitchRampAcrossWordDip = true;
    lastPitch = rawEnd[i];
  }

  // The movements are in Hz: at a low pitch and full inflection a question
  // ended near twice the pitch it was given.
  boundClausePitch(tokens, callerPitch);
}

} // namespace nvsp_frontend
