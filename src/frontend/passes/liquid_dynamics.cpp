\
#include "liquid_dynamics.h"

#include <algorithm>
#include <cmath>

namespace nvsp_frontend::passes {

namespace {

static inline bool tokIsVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool tokIsSilence(const Token& t) {
  return t.silence || !t.def;
}

static inline double clampDouble(double v, double lo, double hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

static inline double clamp01(double v) {
  return clampDouble(v, 0.0, 1.0);
}

static inline double getField(const Token& tok, FieldId fid) {
  const int idx = static_cast<int>(fid);
  const uint64_t bit = 1ULL << idx;
  if (tok.setMask & bit) return tok.field[idx];
  if (tok.def && (tok.def->setMask & bit)) return tok.def->field[idx];
  return 0.0;
}

static inline bool hasField(const Token& tok, FieldId fid) {
  const int idx = static_cast<int>(fid);
  const uint64_t bit = 1ULL << idx;
  return (tok.setMask & bit) || (tok.def && (tok.def->setMask & bit));
}

static inline void setField(Token& tok, FieldId fid, double v) {
  const int idx = static_cast<int>(fid);
  tok.field[idx] = v;
  tok.setMask |= (1ULL << idx);
}

static void clearProsodyForInternalSegment(Token& t) {
  // Internal segments should not start new words/syllables or carry stress/tones.
  t.wordStart = false;
  t.syllableStart = false;
  t.stress = 0;
  t.tone.clear();

  // Prevent later passes from mistaking this as an explicit length mark.
  t.lengthened = false;

  // Tie bars should remain on the outer edges only.
  t.tiedTo = false;
  t.tiedFrom = false;
}

static void clampFadeToDuration(Token& t) {
  if (t.durationMs < 0.0) t.durationMs = 0.0;
  if (t.fadeMs < 0.0) t.fadeMs = 0.0;
  if (t.fadeMs > t.durationMs) t.fadeMs = t.durationMs;
}

} // namespace

bool runLiquidDynamics(
  PassContext& ctx,
  std::vector<Token>& tokens,
  std::string& outError
) {
  (void)outError;

  const auto& lp = ctx.pack.lang;
  if (!lp.liquidDynamicsEnabled) return true;

  const size_t n = tokens.size();
  if (n == 0) return true;

  std::vector<Token> out;
  out.reserve(n + n / 8);

  // Small crossfade used between internal segments.
  const double microFadeMs = std::max(1.0, 2.0 / std::max(0.5, ctx.speed));

  for (size_t i = 0; i < n; ++i) {
    const Token& tok = tokens[i];
    if (tokIsSilence(tok)) {
      out.push_back(tok);
      continue;
    }

    const std::u32string& key = tok.def->key;

    const bool isLateral = (key == U"l" || key == U"ɫ");
    // Only do internal dynamics if the token is long enough to split safely.
    const double dur = tok.durationMs;
    if (dur < 8.0) {
      out.push_back(tok);
      continue;
    }

    // --- /l/ onglide ---
    if (isLateral && lp.liquidDynamicsLateralOnglideDurationPct > 0.0) {
      double pct = clamp01(lp.liquidDynamicsLateralOnglideDurationPct);
      double d1 = clampDouble(dur * pct, 4.0, dur - 4.0);
      double d2 = dur - d1;

      Token a = tok;
      Token b = tok;

      a.durationMs = d1;
      b.durationMs = d2;

      // Keep prosody markers on the first segment only.
      b.wordStart = false;
      b.syllableStart = false;
      b.stress = 0;
      b.tone.clear();
      a.lengthened = false;
      b.lengthened = false;

      // Tie bars: preserve outer edges only.
      b.tiedTo = false;
      a.tiedFrom = false;

      // Fade handling: keep original fade into the first segment,
      // then use a small fade for the internal join.
      a.fadeMs = tok.fadeMs;
      b.fadeMs = microFadeMs;
      clampFadeToDuration(a);
      clampFadeToDuration(b);

      // Apply onglide deltas (only if those fields exist for this token/phoneme).
      const double dF1 = lp.liquidDynamicsLateralOnglideF1Delta;
      const double dF2 = lp.liquidDynamicsLateralOnglideF2Delta;

      if (hasField(a, FieldId::cf1)) setField(a, FieldId::cf1, std::max(0.0, getField(a, FieldId::cf1) + dF1));
      if (hasField(a, FieldId::pf1)) setField(a, FieldId::pf1, std::max(0.0, getField(a, FieldId::pf1) + dF1));
      if (hasField(a, FieldId::cf2)) setField(a, FieldId::cf2, std::max(0.0, getField(a, FieldId::cf2) + dF2));
      if (hasField(a, FieldId::pf2)) setField(a, FieldId::pf2, std::max(0.0, getField(a, FieldId::pf2) + dF2));

      out.push_back(a);
      out.push_back(b);
      continue;
    }

    // Default: unchanged.
    out.push_back(tok);
  }

  tokens.swap(out);
  return true;
}

} // namespace nvsp_frontend::passes
