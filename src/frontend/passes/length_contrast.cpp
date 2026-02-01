#include "length_contrast.h"

#include <algorithm>
#include <cmath>

namespace nvsp_frontend::passes {

namespace {

static inline bool tokIsVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool tokIsConsonant(const Token& t) {
  return t.def && !t.silence && ((t.def->flags & kIsVowel) == 0);
}

static inline bool tokIsStopLike(const Token& t) {
  if (!t.def || t.silence) return false;
  const uint32_t f = t.def->flags;
  return ((f & kIsStop) != 0) || ((f & kIsAfricate) != 0);
}

static inline double clampDouble(double v, double lo, double hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

static inline double safeSpeed(double s) {
  return (s < 0.05) ? 0.05 : s;
}

static void clampFadeToDuration(Token& t) {
  if (t.durationMs < 0.0) t.durationMs = 0.0;
  if (t.fadeMs < 0.0) t.fadeMs = 0.0;
  if (t.fadeMs > t.durationMs) t.fadeMs = t.durationMs;
}

static void scalePrevVowelInWord(std::vector<Token>& tokens, int idxBefore, double scale) {
  if (scale <= 0.0) return;
  bool hitWordStart = false;
  for (int j = idxBefore; j >= 0; --j) {
    Token& t = tokens[j];
    if (t.silence || !t.def) continue;

    if (tokIsVowel(t)) {
      t.durationMs *= scale;
      clampFadeToDuration(t);
      return;
    }

    if (t.wordStart) {
      hitWordStart = true;
    }
    if (hitWordStart) return;
  }
}

} // namespace

bool runLengthContrast(
  PassContext& ctx,
  std::vector<Token>& tokens,
  std::string& outError
) {
  (void)outError;

  const auto& lp = ctx.pack.lang;
  if (!lp.lengthContrastEnabled) return true;

  const double sp = safeSpeed(ctx.speed);

  const double shortCeil = std::max(8.0, lp.lengthContrastShortVowelCeilingMs / sp);
  const double longFloor = std::max(8.0, lp.lengthContrastLongVowelFloorMs / sp);

  // 1) Vowel floors/ceilings.
  for (Token& t : tokens) {
    if (!tokIsVowel(t) || t.silence) continue;

    if (t.lengthened) {
      if (t.durationMs > 0.0 && t.durationMs < longFloor) {
        t.durationMs = longFloor;
        clampFadeToDuration(t);
      }
    } else {
      if (t.durationMs > shortCeil) {
        t.durationMs = shortCeil;
        clampFadeToDuration(t);
      }
    }
  }

  // 2) Gemination cues.
  const double closureScale = clampDouble(lp.lengthContrastGeminateClosureScale, 0.1, 10.0);
  const double releaseScale = clampDouble(lp.lengthContrastGeminateReleaseScale, 0.1, 10.0);
  const double preVScale = clampDouble(lp.lengthContrastPreGeminateVowelScale, 0.1, 10.0);

  const int n = static_cast<int>(tokens.size());
  if (n < 3) return true;

  // 2a) Explicit doubled consonants with an inserted closure gap between them:
  // C [preStopGap] C (same consonant), inside the same word.
  for (int i = 0; i + 2 < n; ++i) {
    Token& c1 = tokens[i];
    Token& gap = tokens[i + 1];
    Token& c2 = tokens[i + 2];

    if (!tokIsConsonant(c1) || !tokIsConsonant(c2)) continue;
    if (!gap.silence || !gap.preStopGap) continue;
    if (!c1.def || !c2.def) continue;
    if (c2.wordStart) continue; // likely across word boundary

    if (c1.def->key != c2.def->key) continue;

    // Lengthen the closure gap (this is the "geminate feel").
    gap.durationMs *= closureScale;
    gap.fadeMs *= closureScale;
    clampFadeToDuration(gap);

    // Slightly shorten the following release portion.
    if (tokIsStopLike(c2)) {
      c2.durationMs *= releaseScale;
      clampFadeToDuration(c2);
    }

    // Compensatory shortening: vowel before geminate tends to shorten.
    scalePrevVowelInWord(tokens, i - 1, preVScale);
  }

  // 2b) Consonants marked lengthened directly (Cː).
  // For STOP consonants, we need to INSERT a closure gap before them,
  // not just lengthen the consonant itself (which sounds wrong).
  // For non-stops (fricatives, nasals, etc.), lengthening the consonant is correct.
  //
  // We collect insertions first, then apply them to avoid iterator invalidation.
  struct GapInsertion {
    int insertBefore;  // index where to insert the gap
    double gapDurationMs;
    double gapFadeMs;
  };
  std::vector<GapInsertion> insertions;

  for (int i = 0; i < n; ++i) {
    Token& c = tokens[i];
    if (!tokIsConsonant(c)) continue;
    if (!c.lengthened) continue;

    if (tokIsStopLike(c)) {
      // For stops/affricates: insert a closure gap BEFORE the consonant.
      // The gap duration should be scaled by closureScale.
      // Use a base gap of ~40ms (typical stop closure) scaled.
      const double baseGapMs = 40.0 / sp;
      const double baseFadeMs = 4.0 / sp;
      
      GapInsertion ins;
      ins.insertBefore = i;
      ins.gapDurationMs = baseGapMs * closureScale;
      ins.gapFadeMs = baseFadeMs;
      insertions.push_back(ins);

      // Optionally shorten the release slightly.
      c.durationMs *= releaseScale;
      clampFadeToDuration(c);
    } else {
      // For non-stops (fricatives, nasals, liquids): just lengthen the sound.
      c.durationMs *= closureScale;
      clampFadeToDuration(c);
    }

    // Compensatory shortening.
    scalePrevVowelInWord(tokens, i - 1, preVScale);

    // Prevent stacking with later passes.
    c.lengthened = false;
  }

  // Apply insertions in reverse order to keep indices valid.
  for (auto it = insertions.rbegin(); it != insertions.rend(); ++it) {
    Token gap;
    gap.silence = true;
    gap.preStopGap = true;
    gap.durationMs = it->gapDurationMs;
    gap.fadeMs = it->gapFadeMs;
    clampFadeToDuration(gap);
    
    tokens.insert(tokens.begin() + it->insertBefore, gap);
  }

  return true;
}

} // namespace nvsp_frontend::passes