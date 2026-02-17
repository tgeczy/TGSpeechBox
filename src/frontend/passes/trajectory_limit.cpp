/*
TGSpeechBox — Trajectory limiting pass (formant rate capping).
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "trajectory_limit.h"

#include <algorithm>
#include <cmath>

namespace nvsp_frontend::passes {

namespace {

static inline bool tokIsSilenceOrMissing(const Token& t) {
  return t.silence || !t.def;
}

// Nasals: ALWAYS skip — place perception (n vs ɲ vs ŋ) depends on sharp
// F2 transitions in adjacent vowels.  Rate limiting destroys the place cue.
static inline bool tokIsNasal(const Token& t) {
  return t.def && ((t.def->flags & kIsNasal) != 0);
}

// Semivowels: ALWAYS skip — the fast glide trajectory IS the percept.
static inline bool tokIsSemivowel(const Token& t) {
  return t.def && ((t.def->flags & kIsSemivowel) != 0);
}

// Liquids: rate-limit but with gentler limits (liquidRateScale multiplier).
static inline bool tokIsLiquid(const Token& t) {
  return t.def && ((t.def->flags & kIsLiquid) != 0);
}

// Map a FieldId index to its transF*Scale group (1=F1, 2=F2, 3=F3).
// Returns 0 for fields not in any formant group.
static inline int transScaleGroup(int fieldIdx) {
  const FieldId fid = static_cast<FieldId>(fieldIdx);
  switch (fid) {
    case FieldId::cf1: case FieldId::pf1:
    case FieldId::cb1: case FieldId::pb1:
      return 1;
    case FieldId::cf2: case FieldId::pf2:
    case FieldId::cb2: case FieldId::pb2:
      return 2;
    case FieldId::cf3: case FieldId::pf3:
    case FieldId::cb3: case FieldId::pb3:
      return 3;
    default:
      return 0;
  }
}

static inline double getResolvedField(const Token& t, int idx) {
  const uint64_t bit = 1ULL << idx;
  if (t.setMask & bit) return t.field[idx];
  if (t.def && (t.def->setMask & bit)) return t.def->field[idx];
  return 0.0;
}

static inline void clampFade(Token& t) {
  if (t.fadeMs < 0.0) t.fadeMs = 0.0;
  if (t.durationMs < 0.0) t.durationMs = 0.0;
  if (t.fadeMs > t.durationMs) t.fadeMs = t.durationMs;
}

}  // namespace

bool runTrajectoryLimit(PassContext& ctx, std::vector<Token>& tokens, std::string& outError) {
  (void)outError;

  const auto& lang = ctx.pack.lang;
  if (!lang.trajectoryLimitEnabled) return true;
  if (tokens.size() < 2) return true;

  const double sp = (ctx.speed > 0.0) ? ctx.speed : 1.0;
  const double win = std::max(0.0, lang.trajectoryLimitWindowMs) / sp;
  if (win <= 0.0) return true;

  const std::uint64_t mask = lang.trajectoryLimitApplyMask;
  if (mask == 0) return true;

  // Maximum fraction of a token's duration that can be fade.
  // If fade exceeds this, the phoneme's "steady state" gets eaten
  // and perception suffers.
  constexpr double kMaxFadeRatio = 0.40;

  for (size_t i = 1; i < tokens.size(); ++i) {
    const Token& prev = tokens[i - 1];
    Token& cur = tokens[i];

    if (tokIsSilenceOrMissing(prev) || tokIsSilenceOrMissing(cur)) {
      continue;
    }

    // Optional: do not smooth across word boundaries.
    if (!lang.trajectoryLimitApplyAcrossWordBoundary && cur.wordStart) {
      continue;
    }

    // Skip nasals and semivowels entirely — they need sharp transitions.
    if (tokIsNasal(cur) || tokIsNasal(prev)) continue;
    if (tokIsSemivowel(cur) || tokIsSemivowel(prev)) continue;

    // Liquids get rate-limited but with gentler limits.
    const bool liquidInvolved = tokIsLiquid(cur) || tokIsLiquid(prev);

    // The fade belongs to the *incoming* token (cur) and is the time over
    // which its targets are interpolated from the previous token.
    const double curFade = std::max(0.001, cur.fadeMs);

    // Get transScale for each formant group.  0.0 means "no override"
    // (= use full fade), so treat 0.0 as 1.0.
    auto effectiveScale = [](double s) -> double {
      return (s > 0.001) ? s : 1.0;
    };
    const double tsF1 = effectiveScale(cur.transF1Scale);
    const double tsF2 = effectiveScale(cur.transF2Scale);
    const double tsF3 = effectiveScale(cur.transF3Scale);
    const double tsArr[4] = {1.0, tsF1, tsF2, tsF3};

    double neededFade = 0.0;

    for (int idx = 0; idx < static_cast<int>(kFrameFieldCount); ++idx) {
      if ((mask & (1ULL << idx)) == 0) continue;

      double effectiveMaxRate = lang.trajectoryLimitMaxHzPerMs[static_cast<size_t>(idx)];
      if (effectiveMaxRate <= 0.0) continue;

      // Liquids get a gentler limit (larger multiplier = more Hz/ms allowed).
      if (liquidInvolved) {
        effectiveMaxRate *= lang.trajectoryLimitLiquidRateScale;
      }

      const double a = getResolvedField(prev, idx);
      const double b = getResolvedField(cur, idx);
      if (a <= 0.0 || b <= 0.0) continue;

      const double delta = std::abs(b - a);
      if (delta <= 1e-6) continue;

      // Account for transScale: effective fade for this formant group
      // is curFade * transScale.  If transScale compresses the fade,
      // the actual Hz/ms rate is higher.
      const double ts = tsArr[transScaleGroup(idx)];
      const double effectiveFade = curFade * ts;
      const double currentRate = delta / std::max(0.001, effectiveFade);

      if (currentRate > effectiveMaxRate) {
        // Need this much effective fade time for this field.
        const double requiredEffective = delta / effectiveMaxRate;
        // Convert back to raw fadeMs: requiredEffective = rawFade * ts
        // → rawFade = requiredEffective / ts
        const double requiredRaw = requiredEffective / std::max(0.001, ts);
        if (requiredRaw > neededFade) neededFade = requiredRaw;
      }
    }

    // Only act if current fade is shorter than what we need.
    if (neededFade > curFade) {
      // Do not extend beyond the configured window.
      double target = std::min(neededFade, win);

      // Also cap fade to a fraction of token duration to preserve steady-state.
      // Use a speed-compensated floor so high speech rates don't starve transitions:
      // at speed 1.0 tokens are ~60ms so the floor never activates,
      // but at high speeds tokens can shrink to ~15ms, crushing needed fade time.
      if (cur.durationMs > 0.0) {
        const double durFloor = 40.0 / sp;
        const double effectiveDur = std::max(cur.durationMs, durFloor);
        const double maxFadeForToken = effectiveDur * kMaxFadeRatio;
        target = std::min(target, maxFadeForToken);
      }

      if (target > cur.fadeMs) {
        cur.fadeMs = target;
        clampFade(cur);
      }
    }
  }

  return true;
}

}  // namespace nvsp_frontend::passes