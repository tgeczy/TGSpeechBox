// =============================================================================
// Special Coarticulation Pass - language-specific Hz deltas
// =============================================================================
//
// Applies configurable formant deltas to vowels adjacent to trigger consonants.
// Rules come from pack YAML (LanguagePack::specialCoarticRules).

#include "special_coartic.h"
#include "../pack.h"
#include "../ipa_engine.h"

#include <algorithm>
#include <cmath>

namespace nvsp_frontend::passes {

namespace {

static inline bool isVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool isSilence(const Token& t) {
  return t.silence || !t.def;
}

static inline double getField(const Token& t, FieldId id) {
  const int idx = static_cast<int>(id);
  if ((t.setMask & (1ULL << idx)) != 0) return t.field[idx];
  if (t.def && (t.def->setMask & (1ULL << idx)) != 0) return t.def->field[idx];
  return 0.0;
}

static inline void setField(Token& t, FieldId id, double val) {
  const int idx = static_cast<int>(id);
  t.field[idx] = val;
  t.setMask |= (1ULL << idx);
}

// Classify vowel as front/back by F2 value.
enum class VowelClass { Front, Back, Mid };

static VowelClass classifyVowel(double f2) {
  if (f2 > 1600.0) return VowelClass::Front;
  if (f2 < 1400.0) return VowelClass::Back;
  return VowelClass::Mid;
}

// Check if a token's IPA key matches any trigger in the rule.
static bool matchesTrigger(const Token& t, const SpecialCoarticRule& rule) {
  if (!t.def) return false;
  const std::u32string& key = t.def->key;
  for (const auto& trig : rule.triggers) {
    if (key == utf8ToU32(trig)) return true;
  }
  return false;
}

// Check if vowel matches the rule's vowelFilter.
static bool matchesVowelFilter(const Token& vowel, double f2, const SpecialCoarticRule& rule) {
  if (rule.vowelFilter == "all") return true;
  if (rule.vowelFilter == "front") return classifyVowel(f2) == VowelClass::Front;
  if (rule.vowelFilter == "back") return classifyVowel(f2) == VowelClass::Back;

  // Specific IPA key match.
  if (vowel.def) {
    std::u32string filterKey = utf8ToU32(rule.vowelFilter);
    if (vowel.def->key == filterKey) return true;
  }
  return false;
}

// Find the nearest non-silence token in a direction.
static int findNeighbor(const std::vector<Token>& tokens, int from, int dir) {
  int idx = from + dir;
  while (idx >= 0 && idx < static_cast<int>(tokens.size())) {
    if (!isSilence(tokens[static_cast<size_t>(idx)])) return idx;
    idx += dir;
  }
  return -1;
}

}  // namespace

bool runSpecialCoarticulation(PassContext& ctx, std::vector<Token>& tokens, std::string& outError) {
  (void)outError;

  const auto& lang = ctx.pack.lang;
  if (!lang.specialCoarticulationEnabled) return true;
  if (lang.specialCoarticRules.empty()) return true;

  const double maxDelta = lang.specialCoarticMaxDeltaHz;

  for (size_t i = 0; i < tokens.size(); ++i) {
    Token& vowel = tokens[i];
    if (!isVowel(vowel)) continue;
    if (isSilence(vowel)) continue;

    const double f2 = getField(vowel, FieldId::cf2);
    if (f2 <= 0.0) continue;

    // Accumulate deltas per formant.
    double accumF2 = 0.0;
    double accumF3 = 0.0;

    const int leftIdx = findNeighbor(tokens, static_cast<int>(i), -1);
    const int rightIdx = findNeighbor(tokens, static_cast<int>(i), +1);

    for (const auto& rule : lang.specialCoarticRules) {
      if (!matchesVowelFilter(vowel, f2, rule)) continue;

      bool leftMatch = false;
      bool rightMatch = false;
      if (leftIdx >= 0 && (rule.side == "left" || rule.side == "both")) {
        leftMatch = matchesTrigger(tokens[static_cast<size_t>(leftIdx)], rule);
      }
      if (rightIdx >= 0 && (rule.side == "right" || rule.side == "both")) {
        rightMatch = matchesTrigger(tokens[static_cast<size_t>(rightIdx)], rule);
      }

      if (!leftMatch && !rightMatch) continue;

      double delta = rule.deltaHz;

      // Apply stress-dependent scaling.
      if (vowel.stress == 0 && rule.unstressedScale != 1.0) {
        delta *= rule.unstressedScale;
      }

      // Phrase-final stressed vowel scaling: applies when vowel is stressed and
      // the next non-silence token is silence or end-of-tokens.
      if (vowel.stress >= 1 && rule.phraseFinalStressedScale != 1.0) {
        bool isPhraseFinal = (rightIdx < 0);
        if (!isPhraseFinal && rightIdx >= 0) {
          // Check if everything after this vowel is silence.
          bool allSilenceAfter = true;
          for (size_t j = i + 1; j < tokens.size(); ++j) {
            if (!isSilence(tokens[j])) { allSilenceAfter = false; break; }
          }
          isPhraseFinal = allSilenceAfter;
        }
        if (isPhraseFinal) {
          delta *= rule.phraseFinalStressedScale;
        }
      }

      // Cumulative: apply from both sides additively.
      int hitCount = 0;
      if (rule.cumulative) {
        if (leftMatch) hitCount++;
        if (rightMatch) hitCount++;
      } else {
        hitCount = 1;
      }

      const double totalDelta = delta * hitCount;

      if (rule.formant == "f2") {
        accumF2 += totalDelta;
      } else if (rule.formant == "f3") {
        accumF3 += totalDelta;
      }
    }

    // Clamp accumulated deltas.
    accumF2 = std::clamp(accumF2, -maxDelta, maxDelta);
    accumF3 = std::clamp(accumF3, -maxDelta, maxDelta);

    // Apply F2 delta.
    if (std::abs(accumF2) > 0.5) {
      double cf2 = getField(vowel, FieldId::cf2);
      double pf2 = getField(vowel, FieldId::pf2);
      if (cf2 > 0.0) setField(vowel, FieldId::cf2, std::max(200.0, cf2 + accumF2));
      if (pf2 > 0.0) setField(vowel, FieldId::pf2, std::max(200.0, pf2 + accumF2));

      // If the locus pass set end targets, adjust those too.
      if (vowel.hasEndCf2) {
        vowel.endCf2 = std::max(200.0, vowel.endCf2 + accumF2);
      }
    }

    // Apply F3 delta.
    if (std::abs(accumF3) > 0.5) {
      double cf3 = getField(vowel, FieldId::cf3);
      double pf3 = getField(vowel, FieldId::pf3);
      if (cf3 > 0.0) setField(vowel, FieldId::cf3, std::max(200.0, cf3 + accumF3));
      if (pf3 > 0.0) setField(vowel, FieldId::pf3, std::max(200.0, pf3 + accumF3));

      if (vowel.hasEndCf3) {
        vowel.endCf3 = std::max(200.0, vowel.endCf3 + accumF3);
      }
    }
  }

  return true;
}

}  // namespace nvsp_frontend::passes
