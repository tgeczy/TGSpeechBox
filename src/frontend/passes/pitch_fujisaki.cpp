// =============================================================================
// Fujisaki Pitch Pass — pitch contour generation
// =============================================================================
//
// Replaces the old calculatePitchesFujisaki() from ipa_engine.cpp.
//
// Architecture: the frontend computes a smoothly declining base pitch using
// exponential decay (no hard floor, no kink).  The DSP's Fujisaki phrase
// and accent filters add local peaks on top.
//
// The original code used linear declination + a hard declinMax floor, which
// created an audible "kink" where the slope suddenly changed.  Exponential
// decay naturally asymptotes — fast initial fall, gradually flattening — so
// long sentences decline smoothly without ever hitting a wall.
//
// Multi-phrase (firing at every word boundary) was tried and reverted: the
// DSP phrase filter peaks at ~193ms and decays quickly, so overlapping humps
// at word boundaries created a "mechanical bull" effect rather than smooth
// declination.  The phrase filter is designed for local emphasis, not for
// creating the overall falling baseline.

#include "pitch_fujisaki.h"
#include "../ipa_engine.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace nvsp_frontend {

void applyPitchFujisaki(
    std::vector<Token>& tokens,
    const PackSet& pack,
    double speed,
    double basePitch,
    double inflection,
    char clauseType) {

  if (tokens.empty()) return;

  const auto& lang = pack.lang;

  // Scale phrase/accent amplitudes by inflection (0..1)
  // At inflection=0, prosody is completely flat
  // At inflection=1, full Fujisaki contour
  const double phraseAmp = lang.fujisakiPhraseAmp * inflection;
  const double primaryAccentAmp = lang.fujisakiPrimaryAccentAmp * inflection;
  const double secondaryAccentAmp = lang.fujisakiSecondaryAccentAmp * inflection;

  // Accent mode: "all", "first_only", or "off"
  const std::string& accentMode = lang.fujisakiAccentMode;
  const bool accentsEnabled = (accentMode != "off");
  const bool firstOnly = (accentMode == "first_only");

  // Clause-type modifiers (ported exactly from original)
  double effectivePhraseAmp = phraseAmp;
  double accentBoost = 1.0;
  double finalRiseAmp = 0.0;   // For questions: accent on final syllable
  double finalDropScale = 0.0; // For exclamations: pitch drop on final syllable
  double declinationMul = 1.0; // Clause-type multiplier for declination rate

  if (clauseType == '?') {
    effectivePhraseAmp *= 0.3;   // Much less phrase arc for questions
    accentBoost = 1.3;           // Stronger accents
    finalRiseAmp = primaryAccentAmp * 2.5;  // Very strong rise at the end
    declinationMul = 0.15;       // Almost flat - questions stay high
    basePitch *= 1.18;           // HIGH pitch for questions (contrast with !)
  } else if (clauseType == '!') {
    effectivePhraseAmp *= 2.5;   // Strong phrase arc for exclamations
    accentBoost = 1.8;           // Strong accents but not overwhelming
    declinationMul = 2.5;        // STEEP declination - dramatic fall
    basePitch *= 1.15;           // Start HIGH - burst of emotion, then fall
    finalDropScale = 0.12;       // SNAP DOWN at end - definitive ending
  } else if (clauseType == ',') {
    effectivePhraseAmp *= 0.5;   // Less phrase arc for commas (continuation)
    declinationMul = 0.4;        // Less declination - incomplete thought stays up
    basePitch *= 1.04;           // Slight raise - continuation feel
  }
  // '.' uses defaults (declinationMul = 1.0) - full declarative fall

  // -------------------------------------------------------------------------
  // Exponential declination
  // -------------------------------------------------------------------------
  // Formula: curPitch = basePitch * exp(-rate * timeMs)
  //
  // rate is derived from inflection and a tuning scale.  Higher inflection
  // means faster pitch fall.  The exponential naturally asymptotes, so there
  // is no hard floor and no "kink" where the slope suddenly changes.
  //
  // At rate=0.0004 and 2000ms: pitch = base * exp(-0.8) ≈ 45% of base range
  // At rate=0.0002 and 2000ms: pitch = base * exp(-0.4) ≈ 67% of base range
  //
  // The inflection slider (0-1) scales this:
  //   inflection=0   → rate=0 (flat)
  //   inflection=0.5 → moderate decline
  //   inflection=1   → full decline
  // -------------------------------------------------------------------------
  double inflScale = lang.legacyPitchInflectionScale;
  if (inflScale <= 0.0) inflScale = 1.0;
  if (inflScale > 2.0) inflScale = 2.0;

  // fujisakiDeclinationRate: new setting, controls the exponential decay
  // steepness.  Default 0.0003 gives natural-sounding decline for typical
  // sentences.  Lower = gentler, higher = steeper.
  double declinRate = lang.fujisakiDeclinationRate;
  if (declinRate <= 0.0) declinRate = 0.0003;  // sane default

  // Length-aware adjustment: slow down declination for longer phrases so
  // the final word still has pitch headroom.  For a reference-length phrase
  // (~1500ms of phonetic content) the configured rate is used as-is.
  // Longer phrases scale the rate down proportionally.
  const double referenceDurationMs = 1500.0;
  double totalPhraseDurationMs = 0.0;
  for (const auto& tok : tokens) {
    if (!tok.silence && tok.def) totalPhraseDurationMs += tok.durationMs;
  }
  const double lengthFactor = referenceDurationMs /
      std::max(referenceDurationMs, totalPhraseDurationMs);

  // Final rate incorporating inflection, scale, clause type, speed, and
  // phrase length.  speed > 1 = faster speech = compress the declination
  // into less time.
  const double effectiveRate = declinRate * inflection * inflScale
      * declinationMul * speed * lengthFactor;

  const int vp = static_cast<int>(FieldId::voicePitch);
  const int evp = static_cast<int>(FieldId::endVoicePitch);

  // First pass: find the last vowel nucleus for clause-final pitch shaping.
  int lastVowelIdx = -1;
  for (int i = static_cast<int>(tokens.size()) - 1; i >= 0; --i) {
    const Token& t = tokens[static_cast<size_t>(i)];
    if (t.silence || !t.def) continue;
    if (tokenIsVowel(t)) {
      lastVowelIdx = i;
      break;
    }
  }
  // Fallback: if we somehow have no vowel, use the last non-silence token.
  if (lastVowelIdx < 0) {
    for (int i = static_cast<int>(tokens.size()) - 1; i >= 0; --i) {
      const Token& t = tokens[static_cast<size_t>(i)];
      if (!t.silence && t.def) {
        lastVowelIdx = i;
        break;
      }
    }
  }

  bool isFirstFrame = true;
  bool hadFirstAccent = false;  // Track if we've placed the first accent
  int pendingStress = 0;        // Stress carried from syllableStart to vowel nucleus
  double durationCounter = 0.0; // Accumulated time for declination

  for (size_t i = 0; i < tokens.size(); ++i) {
    Token& t = tokens[i];
    if (t.silence || !t.def) continue;

    // Exponential declination: smooth, no kink, naturally asymptotes.
    double curBasePitch = basePitch * exp(-effectiveRate * durationCounter);
    double endBasePitch = basePitch * exp(-effectiveRate * (durationCounter + t.durationMs));
    durationCounter += t.durationMs;

    // Set declining base pitch (DSP Fujisaki accents add peaks on top)
    t.field[vp] = curBasePitch;
    t.field[evp] = endBasePitch;
    t.setMask |= (1ull << vp) | (1ull << evp);

    // Enable Fujisaki on all phonetic tokens.
    // Even during unvoiced segments, we still want time to advance so the
    // contour is ready when voicing resumes.
    t.fujisakiEnabled = true;

    // Single phrase command at utterance start.
    // The DSP phrase filter (pitchModel.h) creates a local hump from this
    // impulse — it peaks at ~193ms and decays naturally.  The overall
    // declining baseline comes from the exponential formula above, not
    // from the phrase filter's decay.
    if (isFirstFrame) {
      t.fujisakiReset = true;
      t.fujisakiPhraseAmp = effectivePhraseAmp;
      isFirstFrame = false;
    }

    // Track syllable stress at the syllable boundary...
    if (t.syllableStart) {
      pendingStress = t.stress;
    }

    // ...but place the accent command on the vowel nucleus.
    if (accentsEnabled && tokenIsVowel(t)) {
      bool shouldAccent = false;
      double accentAmp = 0.0;

      // When prominence is available and pitchFromProminence is enabled,
      // use the prominence score to drive accent amplitude.
      if (lang.prominencePitchFromProminence && t.prominence >= 0.0) {
        // Any vowel with prominence > small threshold gets an accent.
        if (t.prominence > 0.05) {
          if (firstOnly) {
            if (!hadFirstAccent) {
              shouldAccent = true;
              hadFirstAccent = true;
            }
          } else {
            shouldAccent = true;
          }
          // Scale accent amplitude by prominence score.
          // prominence 1.0 → primaryAccentAmp, 0.5 → half that, etc.
          accentAmp = primaryAccentAmp * accentBoost * t.prominence;
        }
        pendingStress = 0;
      } else if (pendingStress != 0) {
        // Original stress-mark-based accent logic (unchanged).
        if (pendingStress == 1) {
          if (firstOnly) {
            if (!hadFirstAccent) {
              shouldAccent = true;
              hadFirstAccent = true;
            }
          } else {
            shouldAccent = true;
          }
          accentAmp = primaryAccentAmp * accentBoost;
        } else if (pendingStress == 2 && !firstOnly) {
          shouldAccent = true;
          accentAmp = secondaryAccentAmp * accentBoost;
        }
        pendingStress = 0;
      }

      if (shouldAccent) {
        t.fujisakiAccentAmp = accentAmp;
      }
    }

    // Final vowel: direct pitch shaping for clause-type identity.
    // This is essential for short utterances where exponential declination
    // barely has time to create within-word pitch movement.
    if (static_cast<int>(i) == lastVowelIdx) {
      if (clauseType == '?') {
        // Question: pitch RISES across final vowel.
        // The accent command adds a hump on top, but the base contour
        // itself must end higher to sound like a question.
        t.field[evp] = t.field[vp] * 1.25;  // end 25% higher
        if (finalRiseAmp > 0.0) {
          t.fujisakiAccentAmp = std::max(t.fujisakiAccentAmp, finalRiseAmp);
        }
      } else if (clauseType == '!' && finalDropScale > 0.0) {
        // Exclamation: snap DOWN (existing behavior, preserved exactly)
        t.fujisakiAccentAmp = 0.0;
        double dropFactor = 1.0 + finalDropScale;
        t.field[vp] /= dropFactor;
        t.field[evp] /= (dropFactor * 1.3);
      } else if (clauseType == '.') {
        // Statement: ensure pitch FALLS on final vowel.
        // For long sentences the declination already does this,
        // but for single words we need a direct nudge.
        t.field[evp] *= 0.85;  // end 15% lower
      }
      // Comma: no final shaping (continuation = level/slight rise)
    }
  }
}

} // namespace nvsp_frontend