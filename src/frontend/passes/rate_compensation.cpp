/*
TGSpeechBox — Rate compensation pass.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.

Replaces the old "reduction" pass. Four phases:
  Phase 0: Word-final schwa reduction (phonological, always if enabled)
  Phase 1: Perceptual floor enforcement per phoneme class
  Phase 2: Word-final protection bonus
  Phase 3: Cluster proportion guard
  Phase 4: Rate-dependent schwa shortening (absorbed from old reduction)
*/

#include "rate_compensation.h"

#include <algorithm>
#include <cmath>

namespace nvsp_frontend::passes {

namespace {

// ── Token helpers (local, same pattern as other passes) ──

static inline bool isSilenceOrMissing(const Token& t) {
  return t.silence || !t.def;
}

static inline bool isVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool isNasal(const Token& t) {
  return t.def && ((t.def->flags & kIsNasal) != 0);
}

static inline bool isLiquid(const Token& t) {
  return t.def && ((t.def->flags & kIsLiquid) != 0);
}

static inline bool isSemivowel(const Token& t) {
  return t.def && ((t.def->flags & kIsSemivowel) != 0);
}

static inline bool isAffricate(const Token& t) {
  return t.def && ((t.def->flags & kIsAfricate) != 0);
}

static inline bool isStop(const Token& t) {
  return t.def && ((t.def->flags & kIsStop) != 0);
}

static inline bool isTap(const Token& t) {
  return t.def && ((t.def->flags & kIsTap) != 0);
}

static inline bool isTrill(const Token& t) {
  return t.def && ((t.def->flags & kIsTrill) != 0);
}

static inline bool isFricativeLike(const Token& t) {
  if (!t.def) return false;
  int idx = static_cast<int>(FieldId::fricationAmplitude);
  uint64_t bit = 1ull << idx;
  double v = 0.0;
  if (t.setMask & bit) v = t.field[idx];
  else if (t.def->setMask & bit) v = t.def->field[idx];
  return v > 0.05;
}

static inline bool isSchwaKey(const std::u32string& key) {
  return key == U"@" || key == U"ə";
}

static inline bool isSyntheticGap(const Token& t) {
  return t.preStopGap || t.postStopAspiration || t.vowelHiatusGap;
}

// Check if token at index i is at word-final position.
static bool isWordFinal(const std::vector<Token>& tokens, size_t i) {
  for (size_t j = i + 1; j < tokens.size(); ++j) {
    if (tokens[j].silence) continue;
    return tokens[j].wordStart;
  }
  return true;  // utterance end = word-final
}

// Check if token at index i is penultimate at word end (one real token
// before a word-final consonant).
static bool isPenultimateAtWordEnd(const std::vector<Token>& tokens, size_t i) {
  // Find next non-silence token.
  size_t nextReal = tokens.size();
  for (size_t j = i + 1; j < tokens.size(); ++j) {
    if (!tokens[j].silence) { nextReal = j; break; }
  }
  if (nextReal >= tokens.size()) return false;
  // The next real token must be word-final and non-vowel (consonant).
  if (isVowel(tokens[nextReal])) return false;
  return isWordFinal(tokens, nextReal);
}

// Get the perceptual floor for a token based on its phoneme class.
// Returns 0.0 if no floor applies. Check order matches briefing priority.
static double getClassFloor(const Token& t, const LanguagePack& lang) {
  if (isVowel(t))      return lang.rateCompVowelFloorMs;
  if (isNasal(t))      return lang.rateCompNasalFloorMs;
  if (isLiquid(t))     return lang.rateCompLiquidFloorMs;
  if (isSemivowel(t))  return lang.rateCompSemivowelFloorMs;
  if (isAffricate(t))  return lang.rateCompAffricateFloorMs;
  if (isStop(t))       return lang.rateCompStopFloorMs;
  if (isTap(t))        return lang.rateCompTapFloorMs;
  if (isTrill(t))      return lang.rateCompTrillFloorMs;
  if (isFricativeLike(t)) return lang.rateCompFricativeFloorMs;
  // Default: voiced consonant catch-all.
  return lang.rateCompVoicedConsonantFloorMs;
}

// Apply optional speed scaling to a floor value.
static double scaleFloor(double floor, double speedScale, double speed) {
  if (speedScale <= 0.0) return floor;
  double factor = 1.0 - speedScale * std::min(1.0, (speed - 1.0) / 4.0);
  return floor * factor;
}

}  // namespace

bool runRateCompensation(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError) {
  (void)outError;

  const auto& lang = ctx.pack.lang;

  // ── Phase 0: Word-final schwa reduction (phonological, not rate-related) ──
  // TODO: move to a PostTiming phonological pass when one exists.
  if (lang.wordFinalSchwaReductionEnabled) {
    for (size_t i = 0; i < tokens.size(); ++i) {
      Token& t = tokens[i];
      if (isSilenceOrMissing(t) || !isVowel(t)) continue;
      if (t.stress != 0) continue;
      if (!isSchwaKey(t.def->key)) continue;
      if (!isWordFinal(tokens, i)) continue;

      t.durationMs *= lang.wordFinalSchwaScale;
      t.durationMs = std::max(t.durationMs, lang.wordFinalSchwaMinDurationMs);
    }
  }

  // Early exit if rate compensation is disabled.
  if (!lang.rateCompEnabled) return true;

  // ── Phase 1: Perceptual floor enforcement ──
  // Store original durations for Phase 3 (cluster proportion guard).
  std::vector<double> origDur(tokens.size());
  for (size_t i = 0; i < tokens.size(); ++i) {
    origDur[i] = tokens[i].durationMs;
  }

  for (size_t i = 0; i < tokens.size(); ++i) {
    Token& t = tokens[i];
    if (isSilenceOrMissing(t)) continue;
    if (isSyntheticGap(t)) continue;

    double floor = getClassFloor(t, lang);
    if (floor <= 0.0) continue;

    double effective = scaleFloor(floor, lang.rateCompFloorSpeedScale, ctx.speed);
    if (t.durationMs < effective) {
      t.durationMs = effective;
    }
  }

  // ── Phase 2: Word-final protection ──
  for (size_t i = 0; i < tokens.size(); ++i) {
    Token& t = tokens[i];
    if (isSilenceOrMissing(t)) continue;
    if (isSyntheticGap(t)) continue;

    double floor = getClassFloor(t, lang);
    if (floor <= 0.0) continue;

    double effective = scaleFloor(floor, lang.rateCompFloorSpeedScale, ctx.speed);

    if (isWordFinal(tokens, i)) {
      double wfFloor = effective + lang.rateCompWordFinalBonusMs;
      if (t.durationMs < wfFloor) {
        t.durationMs = wfFloor;
      }
    } else if (isPenultimateAtWordEnd(tokens, i)) {
      double penFloor = effective + lang.rateCompWordFinalBonusMs * 0.5;
      if (t.durationMs < penFloor) {
        t.durationMs = penFloor;
      }
    }
  }

  // ── Phase 3: Cluster proportion guard ──
  if (lang.rateCompClusterProportionGuard) {
    const double maxShift = lang.rateCompClusterMaxRatioShift;

    // Walk adjacent consonant pairs (both non-silence, non-vowel, non-gap).
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
      Token& c1 = tokens[i];
      Token& c2 = tokens[i + 1];

      // Both must be real consonants in the same cluster.
      if (isSilenceOrMissing(c1) || isSilenceOrMissing(c2)) continue;
      if (isVowel(c1) || isVowel(c2)) continue;
      if (isSyntheticGap(c1) || isSyntheticGap(c2)) continue;
      if (c2.wordStart || c2.syllableStart) continue;  // not same cluster

      double orig1 = origDur[i];
      double orig2 = origDur[i + 1];
      if (orig2 <= 0.0 || orig1 <= 0.0) continue;

      double origRatio = orig1 / orig2;
      double newRatio = c1.durationMs / c2.durationMs;
      double shift = newRatio - origRatio;

      if (std::abs(shift) <= maxShift) continue;

      // One segment got bumped disproportionately. Nudge the other
      // toward restoring the original ratio, but never above its
      // original duration (only raise, don't lower).
      if (shift > 0.0) {
        // c1 grew relative to c2 — c2 was the one that stayed small
        // or c1 was the one that got floored. Nudge c2 up.
        double target = c1.durationMs / origRatio;
        double floor2 = getClassFloor(c2, lang);
        double nudged = std::max(c2.durationMs, std::min(target, orig2));
        nudged = std::max(nudged, scaleFloor(floor2, lang.rateCompFloorSpeedScale, ctx.speed));
        c2.durationMs = nudged;
      } else {
        // c2 grew relative to c1. Nudge c1 up.
        double target = c2.durationMs * origRatio;
        double floor1 = getClassFloor(c1, lang);
        double nudged = std::max(c1.durationMs, std::min(target, orig1));
        nudged = std::max(nudged, scaleFloor(floor1, lang.rateCompFloorSpeedScale, ctx.speed));
        c1.durationMs = nudged;
      }
    }
  }

  // ── Phase 4: Rate-dependent schwa shortening ──
  if (lang.rateCompSchwaReductionEnabled &&
      ctx.speed > lang.rateCompSchwaThreshold) {
    const double thr = std::max(0.1, lang.rateCompSchwaThreshold);
    const double over = std::min(1.0, (ctx.speed - thr) / thr);
    const double scale = 1.0 + over * (lang.rateCompSchwaScale - 1.0);

    // Compute the effective vowel floor for clamping.
    double vowelFloor = scaleFloor(
        lang.rateCompVowelFloorMs, lang.rateCompFloorSpeedScale, ctx.speed);

    for (Token& t : tokens) {
      if (isSilenceOrMissing(t) || !isVowel(t)) continue;
      if (t.stress != 0) continue;
      if (!isSchwaKey(t.def->key)) continue;

      t.durationMs *= scale;
      // Floor still enforced — schwa reduction can't create zombies.
      t.durationMs = std::max(t.durationMs, vowelFloor);
    }
  }

  return true;
}

}  // namespace nvsp_frontend::passes
