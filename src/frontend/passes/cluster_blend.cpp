// =============================================================================
// Cluster Blend Pass — C→C articulatory anticipation
// =============================================================================
//
// When two consonants are adjacent, the first consonant's formants should
// begin moving toward the second consonant's place of articulation before
// the crossfade boundary.  This mimics gestural overlap in natural speech:
//
//   "blank" /blæŋk/ — the /ŋ/ starts with velar nasality, but its formants
//   anticipate the /k/ burst.  In isolation /ŋ/ has steady-state formants;
//   before /k/ the velar pinch tightens and nasal amplitude starts fading.
//
// Mechanism:
//   Set endCf1/2/3 on the first consonant (C1) to interpolate partway toward
//   the second consonant's (C2) formant values.  The DSP already ramps from
//   start→end within a single frame — we're just providing the end targets
//   that weren't there before.
//
// Complements:
//   cluster_timing  → adjusts HOW LONG each consonant is
//   boundary_smooth → adjusts HOW FAST the crossfade happens
//   cluster_blend   → adjusts WHAT THE FORMANTS DO during the overlap
//
// Runs PostTiming, after cluster_timing, coarticulation, and special_coartic.

#include "cluster_blend.h"
#include "../pack.h"
#include "../ipa_engine.h"

#include <algorithm>
#include <cmath>

namespace nvsp_frontend::passes {

namespace {

// ── Phoneme classification helpers ──────────────────────────────────────

static inline bool isSilence(const Token& t) {
  return t.silence || !t.def;
}

static inline bool isVowelFlag(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool isConsonant(const Token& t) {
  if (!t.def) return false;
  return (t.def->flags & kIsVowel) == 0;
}

static inline bool isStop(const Token& t) {
  return t.def && ((t.def->flags & kIsStop) != 0);
}

static inline bool isAffricate(const Token& t) {
  return t.def && ((t.def->flags & kIsAfricate) != 0);
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

static inline bool isFricative(const Token& t) {
  if (!t.def) return false;
  if (isStop(t) || isAffricate(t)) return false;
  const int idx = static_cast<int>(FieldId::fricationAmplitude);
  const uint64_t bit = 1ULL << idx;
  double v = 0.0;
  if (t.setMask & bit) v = t.field[idx];
  else if (t.def->setMask & bit) v = t.def->field[idx];
  return v > 0.05;
}

static inline bool isStopLike(const Token& t) {
  return isStop(t) || isAffricate(t);
}

// ── Place of articulation ───────────────────────────────────────────────

enum class Place {
  Unknown,
  Labial,
  Alveolar,
  Palatal,
  Velar,
};

static Place getPlace(const std::u32string& key) {
  if (key == U"p" || key == U"b" || key == U"m" ||
      key == U"f" || key == U"v" || key == U"w" ||
      key == U"ʍ" || key == U"ɸ" || key == U"β")
    return Place::Labial;

  if (key == U"t" || key == U"d" || key == U"n" ||
      key == U"s" || key == U"z" || key == U"l" ||
      key == U"r" || key == U"ɹ" || key == U"ɾ" ||
      key == U"θ" || key == U"ð" || key == U"ɬ" ||
      key == U"ɮ" || key == U"ɻ" || key == U"ɖ" ||
      key == U"ʈ" || key == U"ɳ" || key == U"ɽ")
    return Place::Alveolar;

  if (key == U"ʃ" || key == U"ʒ" || key == U"tʃ" || key == U"t͡ʃ" ||
      key == U"dʒ" || key == U"d͡ʒ" || key == U"j" || key == U"ɲ" ||
      key == U"ç" || key == U"ʝ" || key == U"c" ||
      key == U"ɟ" || key == U"ʎ")
    return Place::Palatal;

  if (key == U"k" || key == U"g" || key == U"ŋ" ||
      key == U"x" || key == U"ɣ" || key == U"ɰ")
    return Place::Velar;

  return Place::Unknown;
}

// ── Consonant manner class (for per-class blend strength) ───────────────

enum class Manner { Stop, Fricative, Nasal, Liquid, Other };

static Manner getManner(const Token& t) {
  if (isStopLike(t))  return Manner::Stop;
  if (isFricative(t)) return Manner::Fricative;
  if (isNasal(t))     return Manner::Nasal;
  if (isLiquid(t) || isSemivowel(t)) return Manner::Liquid;
  return Manner::Other;
}

// ── Formant access helpers ──────────────────────────────────────────────

static double getField(const Token& t, FieldId id) {
  const int idx = static_cast<int>(id);
  if ((t.setMask & (1ULL << idx)) != 0) return t.field[idx];
  if (t.def && (t.def->setMask & (1ULL << idx)) != 0) return t.def->field[idx];
  return 0.0;
}

// Get the best available formant value for a consonant.
// Prefers cascade, falls back to parallel.
static double getFormant(const Token& t, FieldId cf, FieldId pf) {
  double v = getField(t, cf);
  if (v > 0.0) return v;
  return getField(t, pf);
}

// ── Neighbor search (skip micro-gaps, not real silence) ─────────────────

static int findNextConsonant(const std::vector<Token>& tokens, int from) {
  for (int j = from + 1; j < static_cast<int>(tokens.size()); ++j) {
    const Token& t = tokens[static_cast<size_t>(j)];
    // Skip micro-gaps (preStopGap, clusterGap) — these are inserted silence
    // that shouldn't break the cluster.
    if (t.silence && (t.preStopGap || t.clusterGap)) continue;
    if (t.postStopAspiration) continue;  // Part of the stop gesture, not a separate C
    // Real silence or vowel → cluster is over.
    if (isSilence(t)) return -1;
    if (isVowelFlag(t)) return -1;
    if (isConsonant(t)) return j;
  }
  return -1;
}

// ── Per-class blend strength lookup ─────────────────────────────────────

static double getPairStrength(
    Manner m1, Manner m2,
    const LanguagePack& lang)
{
  const double base = lang.clusterBlendStrength;

  // Nasal → Stop (most perceptually important: /ŋk/, /mp/, /nt/, /nd/)
  if (m1 == Manner::Nasal && m2 == Manner::Stop)
    return base * lang.clusterBlendNasalToStopScale;

  // Fricative → Stop (/st/, /sk/, /sp/)
  if (m1 == Manner::Fricative && m2 == Manner::Stop)
    return base * lang.clusterBlendFricToStopScale;

  // Stop → Fricative (/ts/, /ks/, /ps/)
  if (m1 == Manner::Stop && m2 == Manner::Fricative)
    return base * lang.clusterBlendStopToFricScale;

  // Nasal → Fricative (/nf/, /ns/, /mf/)
  if (m1 == Manner::Nasal && m2 == Manner::Fricative)
    return base * lang.clusterBlendNasalToFricScale;

  // Liquid → Stop (/lt/, /rk/, /lp/)
  if (m1 == Manner::Liquid && m2 == Manner::Stop)
    return base * lang.clusterBlendLiquidToStopScale;

  // Liquid → Fricative (/ls/, /rf/)
  if (m1 == Manner::Liquid && m2 == Manner::Fricative)
    return base * lang.clusterBlendLiquidToFricScale;

  // Fricative → Fricative (/sʃ/ across morpheme boundary, rare)
  if (m1 == Manner::Fricative && m2 == Manner::Fricative)
    return base * lang.clusterBlendFricToFricScale;

  // Stop → Stop (/kt/, /pt/, /gd/)
  if (m1 == Manner::Stop && m2 == Manner::Stop)
    return base * lang.clusterBlendStopToStopScale;

  // Everything else: fallback
  return base * lang.clusterBlendDefaultPairScale;
}

}  // namespace

// =============================================================================
// Main pass
// =============================================================================

bool runClusterBlend(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError)
{
  (void)outError;

  const auto& lang = ctx.pack.lang;
  if (!lang.clusterBlendEnabled) return true;
  if (lang.clusterBlendStrength <= 0.0) return true;

  for (int i = 0; i < static_cast<int>(tokens.size()); ++i) {
    Token& c1 = tokens[static_cast<size_t>(i)];
    if (isSilence(c1)) continue;
    if (!isConsonant(c1)) continue;

    const int nextIdx = findNextConsonant(tokens, i);
    if (nextIdx < 0) continue;

    const Token& c2 = tokens[static_cast<size_t>(nextIdx)];

    // ── Classify the pair ──────────────────────────────────────────────

    const Manner m1 = getManner(c1);
    const Manner m2 = getManner(c2);

    const Place p1 = c1.def ? getPlace(c1.def->key) : Place::Unknown;
    const Place p2 = c2.def ? getPlace(c2.def->key) : Place::Unknown;
    const bool homorganic = (p1 != Place::Unknown && p1 == p2);

    // ── Compute blend strength ─────────────────────────────────────────

    double strength = getPairStrength(m1, m2, lang);

    // Homorganic pairs (same place) share a gesture — less spectral
    // anticipation needed, but we still want some for voicing/manner cues.
    if (homorganic) {
      strength *= lang.clusterBlendHomorganicScale;
    }

    // Word boundary: weaken blend when C2 starts a new word (micro-pause).
    if (c2.wordStart) {
      strength *= lang.clusterBlendWordBoundaryScale;
    }

    strength = std::clamp(strength, 0.0, 1.0);
    if (strength < 0.01) continue;

    // ── Read C1 and C2 formants ────────────────────────────────────────

    const double c1f1 = getFormant(c1, FieldId::cf1, FieldId::pf1);
    const double c1f2 = getFormant(c1, FieldId::cf2, FieldId::pf2);
    const double c1f3 = getFormant(c1, FieldId::cf3, FieldId::pf3);

    const double c2f1 = getFormant(c2, FieldId::cf1, FieldId::pf1);
    const double c2f2 = getFormant(c2, FieldId::cf2, FieldId::pf2);
    const double c2f3 = getFormant(c2, FieldId::cf3, FieldId::pf3);

    // Need at least F2 on both sides to do anything useful.
    if (c1f2 <= 0.0 || c2f2 <= 0.0) continue;

    // ── Compute end targets for C1 ────────────────────────────────────

    // Blend C1's formants partway toward C2's values.
    // endCf = c1 + strength * (c2 - c1)
    //
    // Only set endCf if the movement is perceptually meaningful (>15 Hz).
    // This avoids DSP overhead for trivially small ramps.
    constexpr double kMinDeltaHz = 15.0;

    if (c1f2 > 0.0 && c2f2 > 0.0) {
      const double endF2 = c1f2 + strength * (c2f2 - c1f2);
      if (std::abs(endF2 - c1f2) > kMinDeltaHz) {
        c1.hasEndCf2 = true;
        c1.endCf2 = endF2;
      }
    }

    if (c1f1 > 0.0 && c2f1 > 0.0) {
      // F1 blend is gentler — F1 changes are less about place, more about
      // jaw opening.  Use half the strength.
      const double f1Strength = strength * lang.clusterBlendF1Scale;
      const double endF1 = c1f1 + f1Strength * (c2f1 - c1f1);
      if (std::abs(endF1 - c1f1) > kMinDeltaHz) {
        c1.hasEndCf1 = true;
        c1.endCf1 = endF1;
      }
    }

    if (c1f3 > 0.0 && c2f3 > 0.0) {
      const double endF3 = c1f3 + strength * (c2f3 - c1f3);
      if (std::abs(endF3 - c1f3) > kMinDeltaHz) {
        c1.hasEndCf3 = true;
        c1.endCf3 = endF3;
      }
    }

    // ── Optional: nasal amplitude fade for nasal→stop ──────────────────
    //
    // When a nasal precedes a stop (homorganic or not), the velum closes
    // before the oral release.  We can signal this by slightly reducing
    // C1's nasal amplitude at end-of-frame.  The DSP's existing fade
    // handles most of this, but a nudge in the formant direction helps
    // the nasal "reach toward" the stop closure.
    //
    // This is handled by the formant blend above — as formants move toward
    // the stop's values, the perceived nasality naturally decreases.  No
    // extra amplitude manipulation needed at this stage.
  }

  return true;
}

}  // namespace nvsp_frontend::passes
