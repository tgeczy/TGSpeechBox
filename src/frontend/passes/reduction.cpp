#include "reduction.h"

#include <algorithm>

namespace nvsp_frontend::passes {

namespace {

static inline bool isVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool isSilenceOrMissing(const Token& t) {
  return t.silence || !t.def;
}

static inline bool isSchwaKey(const std::u32string& key) {
  return key == U"@" || key == U"ə";
}

// Check if token at index i is at word-final position.
// Word-final = next non-silence token has wordStart, or we're at end.
static bool isWordFinal(const std::vector<Token>& tokens, size_t i) {
  for (size_t j = i + 1; j < tokens.size(); ++j) {
    if (tokens[j].silence) continue;
    // Found next real token - is it a word start?
    return tokens[j].wordStart;
  }
  // No more tokens - we're at utterance end, which is word-final
  return true;
}

}  // namespace

bool runReduction(PassContext& ctx, std::vector<Token>& tokens, std::string& outError) {
  (void)outError;

  const auto& lang = ctx.pack.lang;

  // --- Word-final schwa reduction (always active if enabled) ---
  if (lang.wordFinalSchwaReductionEnabled) {
    for (size_t i = 0; i < tokens.size(); ++i) {
      Token& t = tokens[i];
      if (isSilenceOrMissing(t) || !isVowel(t)) continue;
      if (t.stress != 0) continue;  // only unstressed
      if (!isSchwaKey(t.def->key)) continue;
      if (!isWordFinal(tokens, i)) continue;

      t.durationMs *= lang.wordFinalSchwaScale;
      t.durationMs = std::max(t.durationMs, lang.wordFinalSchwaMinDurationMs);
    }
  }

  // --- Rate-dependent schwa reduction (only at high speeds) ---
  if (!lang.rateReductionEnabled) return true;

  if (ctx.speed <= lang.rateReductionSchwaReductionThreshold) return true;

  const double thr = std::max(0.1, lang.rateReductionSchwaReductionThreshold);
  const double over = std::min(1.0, (ctx.speed - thr) / thr);
  const double scale = 1.0 + over * (lang.rateReductionSchwaScale - 1.0);

  for (Token& t : tokens) {
    if (isSilenceOrMissing(t) || !isVowel(t)) continue;
    if (t.stress != 0) continue;
    if (!isSchwaKey(t.def->key)) continue;

    t.durationMs *= scale;
    t.durationMs = std::max(t.durationMs, lang.rateReductionSchwaMinDurationMs);
  }

  return true;
}

}  // namespace nvsp_frontend::passes
