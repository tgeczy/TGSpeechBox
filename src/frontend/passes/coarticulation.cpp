// =============================================================================
// Coarticulation Pass - locus-based START/END formant transitions
// =============================================================================
//
// Summary:
//   • For vowel-like segments, shift START formants toward a consonant-dependent
//     locus target.
//   • Keep END formants at the canonical vowel targets (via endCf1..3).
//   • DSP ramps cf/pf from start → end across the vowel frame.
//
// Locus targets (MITalk-style):
//   locus = src + k * (trg - src)
// where src are consonant formant targets, trg are vowel targets, and k≈0.42.
//
// Notes / design choices:
//   • We primarily modify the vowel (start targets), not the consonant.
//   • We still allow a small consonant-side adjustment for:
//       - velar fronting ("velar pinch") next to front vowels
//       - slight front-vowel sharpening for certain fricatives (e.g. ʃ/ʒ)
//   • "Graduated" coarticulation reduces strength when consonants intervene.

#include "coarticulation.h"
#include "../pack.h"
#include "../ipa_engine.h"
#include <algorithm>
#include <cmath>

namespace nvsp_frontend::passes {

// -----------------------------------------------------------------------------
// Helper functions
// -----------------------------------------------------------------------------

static inline bool isVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool isConsonant(const Token& t) {
  if (!t.def) return false;
  return (t.def->flags & kIsVowel) == 0;
}

static inline bool isStop(const Token& t) {
  return t.def && ((t.def->flags & kIsStop) != 0);
}

static inline bool isAfricate(const Token& t) {
  return t.def && ((t.def->flags & kIsAfricate) != 0);
}

static inline bool isFricative(const Token& t) {
  if (!t.def) return false;
  if (isStop(t) || isAfricate(t)) return false;
  const int idx = static_cast<int>(FieldId::fricationAmplitude);
  double fric = 0.0;
  if ((t.setMask & (1ULL << idx)) != 0) {
    fric = t.field[idx];
  } else if (t.def->setMask & (1ULL << idx)) {
    fric = t.def->field[idx];
  }
  return fric > 0.05;
}

static inline bool isSemivowel(const Token& t) {
  return t.def && ((t.def->flags & kIsSemivowel) != 0);
}

static inline bool isVowelLike(const Token& t) {
  return isVowel(t) || isSemivowel(t);
}

// Does this consonant trigger coarticulation on an adjacent vowel?
static inline bool triggersCoarticulation(const Token& t) {
  return isFricative(t) || isStop(t) || isAfricate(t);
}

// -----------------------------------------------------------------------------
// Place of articulation
// -----------------------------------------------------------------------------

enum class Place {
  Unknown,
  Labial,
  Alveolar,
  Palatal,
  Velar,
};

static Place getPlace(const std::u32string& key) {
  // Labials
  if (key == U"p" || key == U"b" || key == U"m" ||
      key == U"f" || key == U"v" || key == U"w" ||
      key == U"ʍ" || key == U"ɸ" || key == U"β") {
    return Place::Labial;
  }
  
  // Alveolars
  if (key == U"t" || key == U"d" || key == U"n" ||
      key == U"s" || key == U"z" || key == U"l" ||
      key == U"r" || key == U"ɹ" || key == U"ɾ" ||
      key == U"θ" || key == U"ð" || key == U"ɬ" ||
      key == U"ɮ" || key == U"ɻ" || key == U"ɖ" ||
      key == U"ʈ" || key == U"ɳ" || key == U"ɽ") {
    return Place::Alveolar;
  }
  
  // Palatals / Postalveolars
  if (key == U"ʃ" || key == U"ʒ" || key == U"tʃ" ||
      key == U"dʒ" || key == U"j" || key == U"ɲ" ||
      key == U"ç" || key == U"ʝ" || key == U"c" ||
      key == U"ɟ" || key == U"ʎ") {
    return Place::Palatal;
  }
  
  // Velars
  if (key == U"k" || key == U"g" || key == U"ŋ" ||
      key == U"x" || key == U"ɣ" || key == U"ɰ") {
    return Place::Velar;
  }
  
  return Place::Unknown;
}

// -----------------------------------------------------------------------------
// Locus values by place - now uses lang pack settings with fallback defaults
// -----------------------------------------------------------------------------

static double getLocusF2(Place place, const LanguagePack& lang) {
  switch (place) {
    case Place::Labial:   return lang.coarticulationLabialF2Locus > 0 ?
                                 lang.coarticulationLabialF2Locus : 800.0;
    case Place::Alveolar: return lang.coarticulationAlveolarF2Locus > 0 ?
                                 lang.coarticulationAlveolarF2Locus : 1800.0;
    case Place::Palatal:  return 2300.0;  // TODO: add to lang pack if needed
    case Place::Velar:    return lang.coarticulationVelarF2Locus > 0 ?
                                 lang.coarticulationVelarF2Locus : 1200.0;
    default:              return 0.0;
  }
}

// F1 locus is generally constricted (low) for all consonants
static double getLocusF1(Place place) {
  switch (place) {
    case Place::Labial:   return 300.0;
    case Place::Alveolar: return 350.0;
    case Place::Palatal:  return 280.0;
    case Place::Velar:    return 320.0;
    default:              return 0.0;
  }
}

// F3 locus isn't as cleanly "place-locked" as F2, but having a fallback helps
// avoid 0 values when doing MITalk-style interpolation.
static double getLocusF3(Place place, const LanguagePack& lang) {
  (void)lang;
  switch (place) {
    case Place::Labial:   return 2600.0;
    case Place::Alveolar: return 2600.0;
    case Place::Palatal:  return 2700.0;
    case Place::Velar:    return 2500.0;
    default:              return 0.0;
  }
}

// -----------------------------------------------------------------------------
// Field access helpers
// -----------------------------------------------------------------------------

static double getField(const Token& t, FieldId id) {
  const int idx = static_cast<int>(id);
  if ((t.setMask & (1ULL << idx)) != 0) {
    return t.field[idx];
  }
  if (t.def && (t.def->setMask & (1ULL << idx)) != 0) {
    return t.def->field[idx];
  }
  return 0.0;
}

static void setField(Token& t, FieldId id, double val) {
  const int idx = static_cast<int>(id);
  t.field[idx] = val;
  t.setMask |= (1ULL << idx);
}

static inline double clampPos(double v) {
  return (v > 0.0) ? v : 0.0;
}

// Prefer cascade formant if set; fall back to parallel; else 0.
static double getCanonicalFormant(const Token& t, FieldId cf, FieldId pf) {
  double v = getField(t, cf);
  if (v <= 0.0) v = getField(t, pf);
  return v;
}

// For consonant "src" formants: prefer cf/pf, then fall back to place locus.
static double getConsonantSrcFormant(const Token& c, FieldId cf, FieldId pf, double placeFallback) {
  double v = getField(c, cf);
  if (v <= 0.0) v = getField(c, pf);
  if (v <= 0.0) v = placeFallback;
  return v;
}

// MITalk-style locus target.
static double mitalkLocus(double src, double trg, double k) {
  if (src <= 0.0 || trg <= 0.0) return 0.0;
  return src + k * (trg - src);
}

// -----------------------------------------------------------------------------
// Velar pinch (modify consonant formants before front vowels)
// -----------------------------------------------------------------------------

static bool isFrontVowel(double f2, const LanguagePack& lang) {
  // Use the velar pinch threshold as the general "front vowel" cutoff, with
  // a safe fallback.
  const double thr = (lang.coarticulationVelarPinchThreshold > 0.0)
                         ? lang.coarticulationVelarPinchThreshold
                         : 1600.0;
  return f2 > thr;
}

static void applyVelarPinch(Token& c, const Token& vowel, 
                            const LanguagePack& lang, double strength) {
  double vowelF2 = getField(vowel, FieldId::cf2);
  if (vowelF2 <= 0.0) vowelF2 = getField(vowel, FieldId::pf2);
  
  if (!isFrontVowel(vowelF2, lang)) {
    return;  // Back vowel - no pinch
  }
  
  // Front vowel - F2 and F3 converge
  const double pinchF2 = vowelF2 * lang.coarticulationVelarPinchF2Scale;
  const double pinchF3 = lang.coarticulationVelarPinchF3;

  auto blendToward = [&](FieldId id, double target) {
    double cur = getField(c, id);
    if (cur <= 0.0) cur = target;
    setField(c, id, cur + (target - cur) * strength);
  };

  blendToward(FieldId::cf2, pinchF2);
  blendToward(FieldId::pf2, pinchF2);

  if (pinchF3 > 0.0) {
    blendToward(FieldId::cf3, pinchF3);
    blendToward(FieldId::pf3, pinchF3);
  }
}

// "Labialized" / palato-alveolar fricative sharpening next to front vowels.
// (Example: ʃ can sound slightly "higher" next to front vowels.)
static void applyFrontVowelFricativeSharpening(Token& c, const Token& vowel,
                                               const LanguagePack& lang, double strength) {
  if (!lang.coarticulationLabializedFricativeFrontingEnabled) return;
  if (!c.def) return;

  // Only apply to a small whitelist.
  const std::u32string& key = c.def->key;
  const bool isTarget = (key == U"ʃ" || key == U"ʒ");
  if (!isTarget) return;

  double vowelF2 = getCanonicalFormant(vowel, FieldId::cf2, FieldId::pf2);
  if (!isFrontVowel(vowelF2, lang)) return;

  // Pull consonant F2 upward slightly toward the following vowel.
  const double pull = std::clamp(lang.coarticulationLabializedFricativeF2Pull, 0.0, 1.0);
  if (pull <= 0.0) return;

  auto pullToward = [&](FieldId id) {
    double cur = getField(c, id);
    if (cur <= 0.0) return;
    setField(c, id, cur + (vowelF2 - cur) * (strength * pull));
  };

  pullToward(FieldId::cf2);
  pullToward(FieldId::pf2);
}

// =============================================================================
// Main coarticulation pass - DECTalk-style START/END transitions
// =============================================================================

bool runCoarticulation(PassContext& ctx, std::vector<Token>& tokens, std::string& outError) {
  (void)outError;

  const auto& lang = ctx.pack.lang;
  if (!lang.coarticulationEnabled) return true;

  const double strength = std::clamp(lang.coarticulationStrength, 0.0, 1.0);
  if (strength <= 0.0) return true;

  for (size_t i = 0; i < tokens.size(); ++i) {
    Token& t = tokens[i];
    if (t.silence) continue;

    // ----- Consonant-side tweaks (allowed exceptions) -----
    // 1) Velar pinch (modify velar consonant before front vowels)
    if (isConsonant(t) && t.def) {
      Place place = getPlace(t.def->key);
      if (place == Place::Velar && lang.coarticulationVelarPinchEnabled) {
        // Find vowel to the right
        for (size_t j = i + 1; j < tokens.size(); ++j) {
          const Token& next = tokens[j];
          if (next.silence) continue;
          if (isVowelLike(next)) {
            applyVelarPinch(t, next, lang, strength);
          }
          break;
        }
      }

      // 2) Slight sharpening for ʃ/ʒ next to front vowels.
      //    (This is intentionally subtle and only triggers on a whitelist.)
      for (size_t j = i + 1; j < tokens.size(); ++j) {
        const Token& next = tokens[j];
        if (next.silence) continue;
        if (isVowelLike(next)) {
          applyFrontVowelFricativeSharpening(t, next, lang, strength);
        }
        break;
      }
    }

    // ----- Vowel coarticulation (locus-based start/end) -----
    if (!isVowelLike(t)) continue;
    
    // Find the nearest consonant to the left (for alveolar-back-vowel rule),
    // and the nearest *triggering* consonant to the left (for standard locus).
    const Token* nearestCons = nullptr;
    const Token* triggerCons = nullptr;
    Place nearestPlace = Place::Unknown;
    Place triggerPlace = Place::Unknown;
    int consonantDistance = 0;  // # of intervening consonants between vowel and trigger

    const int maxAdj = std::max(0, (int)std::round(lang.coarticulationAdjacencyMaxConsonants));
    int nonTriggerCount = 0;

    for (size_t j = i; j > 0; --j) {
      const Token& prev = tokens[j - 1];
      if (prev.silence) break;          // Don't coarticulate across explicit silence.
      if (isVowelLike(prev)) break;     // Stop at previous vowel nucleus.
      if (!isConsonant(prev) || !prev.def) continue;

      if (!nearestCons) {
        nearestCons = &prev;
        nearestPlace = getPlace(prev.def->key);
      }

      if (triggersCoarticulation(prev)) {
        triggerCons = &prev;
        triggerPlace = getPlace(prev.def->key);
        consonantDistance = nonTriggerCount;
        break;
      }

      nonTriggerCount++;
      if (maxAdj > 0 && nonTriggerCount > maxAdj) break;
    }

    // Canonical vowel targets.
    const double vF1 = getCanonicalFormant(t, FieldId::cf1, FieldId::pf1);
    const double vF2 = getCanonicalFormant(t, FieldId::cf2, FieldId::pf2);
    const double vF3 = getCanonicalFormant(t, FieldId::cf3, FieldId::pf3);
    if (vF2 <= 0.0) continue;

    const bool isBackVowel = (vF2 > 0.0 && vF2 < lang.coarticulationBackVowelF2Threshold);

    // Decide which consonant (if any) is influencing this vowel.
    const Token* leftCons = triggerCons;
    Place leftPlace = triggerPlace;
    bool isAlveolarBackVowelRule = false;

    if (!leftCons) {
      // Special case: alveolar sounds can front back vowels (e.g., "new", "suzie").
      if (lang.coarticulationAlveolarBackVowelEnabled && isBackVowel &&
          nearestCons && nearestPlace == Place::Alveolar) {
        leftCons = nearestCons;
        leftPlace = nearestPlace;
        consonantDistance = 0;
        isAlveolarBackVowelRule = true;
      }
    }

    if (!leftCons || leftPlace == Place::Unknown) continue;
    
    // Apply graduated strength falloff if enabled (clusters / non-adjacent triggers).
    double effectiveStrength = strength;
    if (lang.coarticulationGraduated && consonantDistance > 0) {
      const int maxCons = std::max(1, (int)std::round(lang.coarticulationAdjacencyMaxConsonants));
      const double df = std::pow(0.6, std::min(consonantDistance, maxCons));
      effectiveStrength *= df;
    }

    // Special-case boost: alveolar + back vowel fronting tends to be more audible.
    if (isAlveolarBackVowelRule) {
      effectiveStrength *= std::clamp(lang.coarticulationAlveolarBackVowelStrengthBoost, 0.5, 2.0);
    }
    effectiveStrength = std::clamp(effectiveStrength, 0.0, 1.0);
    if (effectiveStrength <= 0.0) continue;

    // Consonant "src" formants.
    const double srcF1 = getConsonantSrcFormant(*leftCons, FieldId::cf1, FieldId::pf1, getLocusF1(leftPlace));
    double srcF2 = getConsonantSrcFormant(*leftCons, FieldId::cf2, FieldId::pf2, getLocusF2(leftPlace, lang));
    const double srcF3 = getConsonantSrcFormant(*leftCons, FieldId::cf3, FieldId::pf3, getLocusF3(leftPlace, lang));

    // Alveolar back-vowel rule: ensure the source locus is sufficiently alveolar.
    if (isAlveolarBackVowelRule) {
      const double alveolarLocus = getLocusF2(Place::Alveolar, lang);
      if (alveolarLocus > 0.0) srcF2 = std::max(srcF2, alveolarLocus);
    }

    // MITalk locus targets.
    const double k = std::clamp(lang.coarticulationMitalkK, 0.0, 1.0);
    const double locusF2 = mitalkLocus(srcF2, vF2, k);
    const double locusF1 = mitalkLocus(srcF1, vF1, k);
    const double locusF3 = mitalkLocus(srcF3, vF3, k);
    if (locusF2 <= 0.0) continue;

    // Per-formant scaling: F2 is the main perceptual cue; keep F1/F3 gentler.
    const double f2Scale = std::clamp(lang.coarticulationF2Scale, 0.0, 2.0);
    const double f1Scale = std::clamp(lang.coarticulationF1Scale, 0.0, 2.0);
    const double f3Scale = std::clamp(lang.coarticulationF3Scale, 0.0, 2.0);

    const double startF2 = vF2 + (locusF2 - vF2) * (effectiveStrength * f2Scale);
    double startF1 = vF1;
    if (vF1 > 0.0 && locusF1 > 0.0) {
      startF1 = vF1 + (locusF1 - vF1) * (effectiveStrength * f1Scale);
    }
    double startF3 = vF3;
    if (vF3 > 0.0 && locusF3 > 0.0) {
      startF3 = vF3 + (locusF3 - vF3) * (effectiveStrength * f3Scale);
    }
    
    // Set START formants (the token's main formant values)
    setField(t, FieldId::cf2, startF2);
    setField(t, FieldId::pf2, startF2);
    if (vF1 > 0.0) {
      setField(t, FieldId::cf1, startF1);
      setField(t, FieldId::pf1, startF1);
    }
    if (vF3 > 0.0) {
      setField(t, FieldId::cf3, startF3);
      setField(t, FieldId::pf3, startF3);
    }
    
    // Set END formants (DSP will ramp from start to end)
    // Only set if there's meaningful movement (avoid overhead for tiny changes)
    if (std::abs(vF2 - startF2) > 10.0) {
      t.hasEndCf2 = true;
      t.endCf2 = vF2;
    }
    if (vF1 > 0.0 && std::abs(vF1 - startF1) > 8.0) {
      t.hasEndCf1 = true;
      t.endCf1 = vF1;
    }
    if (vF3 > 0.0 && std::abs(vF3 - startF3) > 12.0) {
      t.hasEndCf3 = true;
      t.endCf3 = vF3;
    }
  }

  return true;
}

}  // namespace nvsp_frontend::passes
