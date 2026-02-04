// =============================================================================
// Coarticulation Pass - DECTalk-style START/END formant transitions
// =============================================================================
//
// This approach shifts vowel START formants toward the preceding consonant's
// locus, then sets endCf to the canonical vowel target. The DSP ramps smoothly
// from start to end within the vowel, creating natural CV transitions.
//
// This is how DECTalk/Klatt handle coarticulation: each vowel has both START
// and END formant targets, with the START influenced by consonant context.
//
// Rules:
// 1. Find vowels with consonants to the left
// 2. Shift vowel's START F1/F2 toward consonant's locus
// 3. Set endCf1/2 to the vowel's canonical (original) values
// 4. DSP exponentially smooths from start → end over vowel duration
//

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
                                 lang.coarticulationLabialF2Locus : 900.0;
    case Place::Alveolar: return lang.coarticulationAlveolarF2Locus > 0 ? 
                                 lang.coarticulationAlveolarF2Locus : 1700.0;
    case Place::Palatal:  return 2300.0;  // TODO: add to lang pack if needed
    case Place::Velar:    return lang.coarticulationVelarF2Locus > 0 ? 
                                 lang.coarticulationVelarF2Locus : 1500.0;
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

// -----------------------------------------------------------------------------
// Velar pinch (modify consonant formants before front vowels)
// -----------------------------------------------------------------------------

static bool isFrontVowel(double f2) {
  return f2 > 1600.0;  // Front vowels have high F2
}

static void applyVelarPinch(Token& c, const Token& vowel, 
                            const LanguagePack& lang, double strength) {
  double vowelF2 = getField(vowel, FieldId::cf2);
  if (vowelF2 <= 0.0) vowelF2 = getField(vowel, FieldId::pf2);
  
  if (!isFrontVowel(vowelF2)) {
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

    // ----- Velar pinch (modify consonant before front vowels) -----
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
    }

    // ----- Vowel coarticulation (DECTalk-style start/end) -----
    if (!isVowelLike(t)) continue;
    
    // Find consonant immediately to the left, counting distance
    const Token* leftCons = nullptr;
    Place leftPlace = Place::Unknown;
    int consonantDistance = 0;  // How many consonants between vowel and triggering consonant
    
    for (size_t j = i; j > 0; --j) {
      const Token& prev = tokens[j - 1];
      if (prev.silence) break;  // Hit silence, stop looking
      if (isConsonant(prev) && prev.def && triggersCoarticulation(prev)) {
        leftCons = &prev;
        leftPlace = getPlace(prev.def->key);
        break;
      }
      if (isConsonant(prev)) {
        consonantDistance++;  // Count intervening consonants
      }
      if (isVowelLike(prev)) break;  // Hit another vowel, stop
    }
    
    if (!leftCons || leftPlace == Place::Unknown) continue;
    
    // Apply graduated strength if enabled
    double effectiveStrength = strength;
    if (lang.coarticulationGraduated && consonantDistance > 0) {
      // Scale down strength based on distance
      // At max consonants, strength drops to ~30% of original
      double maxCons = lang.coarticulationAdjacencyMaxConsonants;
      if (maxCons > 0) {
        double distanceFactor = 1.0 - (0.7 * std::min((double)consonantDistance, maxCons) / maxCons);
        effectiveStrength *= distanceFactor;
      }
    }
    
    // Get locus values for this place
    double locusF2 = getLocusF2(leftPlace, lang);
    double locusF1 = getLocusF1(leftPlace);
    if (locusF2 <= 0.0) continue;
    
    // Get vowel's canonical (original) formants
    double canonicalF2 = getField(t, FieldId::cf2);
    if (canonicalF2 <= 0.0) canonicalF2 = getField(t, FieldId::pf2);
    if (canonicalF2 <= 0.0) continue;
    
    double canonicalF1 = getField(t, FieldId::cf1);
    if (canonicalF1 <= 0.0) canonicalF1 = getField(t, FieldId::pf1);
    
    // Calculate START formants (shifted toward locus)
    // Use conservative strength - too much makes vowels sound wrong
    // e.g., "chevron" becoming "chayvron" if palatal locus pulls too hard
    double startF2 = canonicalF2 + (locusF2 - canonicalF2) * effectiveStrength * 0.25;
    double startF1 = canonicalF1;
    if (canonicalF1 > 0.0 && locusF1 > 0.0) {
      startF1 = canonicalF1 + (locusF1 - canonicalF1) * effectiveStrength * 0.15;
    }
    
    // Set START formants (the token's main formant values)
    setField(t, FieldId::cf2, startF2);
    setField(t, FieldId::pf2, startF2);
    if (canonicalF1 > 0.0) {
      setField(t, FieldId::cf1, startF1);
      setField(t, FieldId::pf1, startF1);
    }
    
    // Set END formants (DSP will ramp from start to end)
    // Only set if there's meaningful movement (avoid overhead for tiny changes)
    if (std::abs(canonicalF2 - startF2) > 20.0) {
      t.hasEndCf2 = true;
      t.endCf2 = canonicalF2;
    }
    if (canonicalF1 > 0.0 && std::abs(canonicalF1 - startF1) > 15.0) {
      t.hasEndCf1 = true;
      t.endCf1 = canonicalF1;
    }
  }

  return true;
}

}  // namespace nvsp_frontend::passes
