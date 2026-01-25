#include "nasalization.h"

#include <algorithm>

namespace nvsp_frontend::passes {

namespace {

static inline bool isVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool isNasal(const Token& t) {
  if (!t.def) return false;
  if ((t.def->flags & kIsVowel) != 0) return false;
  return (t.def->flags & kIsNasal) != 0;
}

static inline bool hasField(const Token& t, FieldId id) {
  return (t.setMask & (1ULL << static_cast<int>(id))) != 0;
}

static inline Token* nextNonSilence(std::vector<Token>& tokens, size_t start) {
  for (size_t i = start; i < tokens.size(); ++i) {
    if (tokens[i].silence || !tokens[i].def) continue;
    return &tokens[i];
  }
  return nullptr;
}

}  // namespace

bool runNasalization(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& /*outError*/) {
  const auto& lang = ctx.pack.lang;
  if (!lang.nasalizationAnticipatoryEnabled) {
    return true;
  }

  const double targetCoupling = std::clamp(lang.nasalizationAnticipatoryAmplitude, 0.0, 1.0);
  if (targetCoupling <= 0.0) {
    return true;
  }

  for (size_t i = 0; i + 1 < tokens.size(); ++i) {
    Token& v = tokens[i];
    if (v.silence || !v.def) continue;
    if (!isVowel(v)) continue;

    Token* n = nextNonSilence(tokens, i + 1);
    if (!n) continue;
    if (n->wordStart) {
      // Don't nasalize across word boundaries by default.
      continue;
    }
    if (!isNasal(*n)) continue;

    const int caNpIdx = static_cast<int>(FieldId::caNP);
    double cur = v.field[caNpIdx];

    // If the vowel already has nasal coupling, we only nudge it upward.
    double next = std::max(cur, targetCoupling);

    // Make it gentle, not “full nasal”.
    // (This avoids wrecking vowels in languages that don't nasalize strongly.)
    const double blend = std::clamp(lang.nasalizationAnticipatoryBlend, 0.0, 1.0);
    v.field[caNpIdx] = cur + (next - cur) * blend;

    // Ensure caNP is considered set for this token.
    v.setMask |= (1ULL << caNpIdx);
  }

  return true;
}

}  // namespace nvsp_frontend::passes
