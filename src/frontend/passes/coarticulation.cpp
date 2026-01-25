#include "coarticulation.h"

#include <algorithm>
#include <cmath>

namespace nvsp_frontend::passes {

namespace {

static inline bool hasField(const Token& t, FieldId id) {
  return (t.setMask & (1ULL << static_cast<int>(id))) != 0;
}

static inline bool isVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool isStopLike(const Token& t) {
  if (!t.def) return false;
  const uint32_t f = t.def->flags;
  return ((f & kIsStop) != 0) || ((f & kIsAfricate) != 0);
}

static inline bool isVelarKey(const std::u32string& key) {
  return key == U"k" || key == U"g" || key == U"ŋ";
}

static inline bool isLabialKey(const std::u32string& key) {
  return key == U"p" || key == U"b" || key == U"m" || key == U"f" || key == U"v" || key == U"w";
}

static inline bool isAlveolarKey(const std::u32string& key) {
  return key == U"t" || key == U"d" || key == U"n" || key == U"s" || key == U"z" || key == U"l" || key == U"r" || key == U"ɾ";
}

// Read a token's F2 in Hz, preferring cascade (cf2) then parallel (pf2).
static double getTokenF2(const Token& t) {
  const int cf2 = static_cast<int>(FieldId::cf2);
  const int pf2 = static_cast<int>(FieldId::pf2);

  if (hasField(t, FieldId::cf2) && t.field[cf2] > 0.0) return t.field[cf2];
  if (hasField(t, FieldId::pf2) && t.field[pf2] > 0.0) return t.field[pf2];
  // Fall back to cf1/pf1 heuristic? Not helpful; return 0.
  return 0.0;
}

static void shiftFieldToward(Token& t, FieldId id, double target, double strength) {
  if (!hasField(t, id)) return;
  const int idx = static_cast<int>(id);
  const double cur = t.field[idx];
  if (cur <= 0.0) {
    t.field[idx] = target;
    return;
  }
  t.field[idx] = cur + (target - cur) * strength;
}

static const Token* findAdjacentVowelLeft(const std::vector<Token>& tokens, size_t i) {
  // Look left, skipping silence; stop at word boundary.
  for (size_t j = i; j > 0; --j) {
    const Token& prev = tokens[j - 1];
    if (prev.silence || !prev.def) continue;
    if (isVowel(prev)) return &prev;
    // If we hit the start of the word (and it's not a vowel), don't cross into the previous word.
    if (prev.wordStart) return nullptr;
    // Only look through one consonant.
    return nullptr;
  }
  return nullptr;
}

static const Token* findAdjacentVowelRight(const std::vector<Token>& tokens, size_t i) {
  for (size_t j = i + 1; j < tokens.size(); ++j) {
    const Token& next = tokens[j];
    if (next.silence || !next.def) continue;
    if (isVowel(next)) return &next;
    // Stop at word boundary.
    if (next.wordStart) return nullptr;
    // Only look through one consonant.
    return nullptr;
  }
  return nullptr;
}

}  // namespace

bool runCoarticulation(PassContext& ctx, std::vector<Token>& tokens, std::string& outError) {
  (void)outError;

  const auto& lang = ctx.pack.lang;
  if (!lang.coarticulationEnabled) return true;

  const double strength = std::clamp(lang.coarticulationStrength, 0.0, 1.0);
  const double extent = std::clamp(lang.coarticulationTransitionExtent, 0.0, 1.0);

  for (size_t i = 0; i < tokens.size(); ++i) {
    Token& c = tokens[i];
    if (c.silence || !c.def) continue;
    if (!isStopLike(c)) continue;

    const std::u32string& key = c.def->key;

    double locusF2 = 0.0;
    double pinchF3 = 0.0;
    bool isVelar = false;

    if (isLabialKey(key)) {
      locusF2 = lang.coarticulationLabialF2Locus;
    } else if (isAlveolarKey(key)) {
      locusF2 = lang.coarticulationAlveolarF2Locus;
    } else if (isVelarKey(key)) {
      locusF2 = lang.coarticulationVelarF2Locus;
      isVelar = true;
    } else {
      continue;  // No locus for this consonant.
    }

    const Token* nextV = findAdjacentVowelRight(tokens, i);

    // Velar pinch: /k,g,ŋ/ before front vowels.
    if (isVelar && lang.coarticulationVelarPinchEnabled && nextV) {
      const double vF2 = getTokenF2(*nextV);
      if (vF2 > lang.coarticulationVelarPinchThreshold) {
        locusF2 = vF2 * lang.coarticulationVelarPinchF2Scale;
        pinchF3 = lang.coarticulationVelarPinchF3;
      }
    }

    // Apply locus shift to whichever formant tracks the consonant already uses.
    // (We avoid forcing fields that the phoneme didn't define, to reduce cross-language surprises.)
    shiftFieldToward(c, FieldId::cf2, locusF2, strength);
    shiftFieldToward(c, FieldId::pf2, locusF2, strength);

    if (pinchF3 > 0.0) {
      shiftFieldToward(c, FieldId::cf3, pinchF3, strength);
      shiftFieldToward(c, FieldId::pf3, pinchF3, strength);
    }

    // Optional: slightly longer fade INTO the consonant, so vowel->stop transitions don't “click”.
    // Fade is stored per-token as the transition time from the previous token INTO this one.
    if (lang.coarticulationFadeIntoConsonants && extent > 0.0 && c.durationMs > 0.0) {
      double minFade = c.durationMs * extent;
      // If this stop is word-initial, keep a smaller fade so the onset stays crisp.
      if (c.wordStart) minFade *= lang.coarticulationWordInitialFadeScale;
      c.fadeMs = std::max(c.fadeMs, minFade);
      if (c.fadeMs > c.durationMs) c.fadeMs = c.durationMs;
    }

    // (We currently don't touch vowel fades, to avoid the “fade-in on vowels” artifact.)
  }

  return true;
}

}  // namespace nvsp_frontend::passes
