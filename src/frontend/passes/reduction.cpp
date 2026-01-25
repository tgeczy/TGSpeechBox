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

}  // namespace

bool runReduction(PassContext& ctx, std::vector<Token>& tokens, std::string& outError) {
  (void)outError;

  const auto& lang = ctx.pack.lang;
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
