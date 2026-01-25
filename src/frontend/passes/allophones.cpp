\
#include "allophones.h"

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

static inline bool tokIsVoiced(const Token& t) {
  return t.def && ((t.def->flags & kIsVoiced) != 0);
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

static inline double safeSpeed(double s) {
  return (s < 0.05) ? 0.05 : s;
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

static void clampFadeToDuration(Token& t) {
  if (t.durationMs < 0.0) t.durationMs = 0.0;
  if (t.fadeMs < 0.0) t.fadeMs = 0.0;
  if (t.fadeMs > t.durationMs) t.fadeMs = t.durationMs;
}

static const PhonemeDef* findPhoneme(const PackSet& pack, const std::u32string& key) {
  auto it = pack.phonemes.find(key);
  if (it == pack.phonemes.end()) return nullptr;
  return &it->second;
}

static bool containsString(const std::vector<std::string>& v, const char* s) {
  for (const auto& x : v) {
    if (x == s) return true;
  }
  return false;
}

static const Token* prevNonSilence(const std::vector<Token>& tokens, int i) {
  for (int j = i - 1; j >= 0; --j) {
    if (!tokIsSilence(tokens[j])) return &tokens[j];
  }
  return nullptr;
}

static const Token* nextNonSilence(const std::vector<Token>& tokens, int i) {
  const int n = static_cast<int>(tokens.size());
  for (int j = i + 1; j < n; ++j) {
    if (!tokIsSilence(tokens[j])) return &tokens[j];
  }
  return nullptr;
}

static bool isWordFinalIndex(const std::vector<Token>& tokens, int i) {
  const int n = static_cast<int>(tokens.size());
  for (int j = i + 1; j < n; ++j) {
    const Token& t = tokens[j];
    if (tokIsSilence(t)) continue;
    return t.wordStart;
  }
  return true; // end of stream
}

} // namespace

bool runAllophones(
  PassContext& ctx,
  std::vector<Token>& tokens,
  std::string& outError
) {
  (void)outError;

  const auto& lp = ctx.pack.lang;
  if (!lp.positionalAllophonesEnabled) return true;

  const double sp = safeSpeed(ctx.speed);

  // 1) Stop aspiration scaling (operates on the inserted aspiration token)
  for (int i = 0; i < static_cast<int>(tokens.size()); ++i) {
    Token& t = tokens[i];
    if (t.silence || !t.def) continue;
    if (!t.postStopAspiration) continue;

    const Token* prev = prevNonSilence(tokens, i);
    const Token* next = nextNonSilence(tokens, i);

    if (!prev || !prev->def) continue;

    const bool prevIsStop = tokIsStopLike(*prev);
    if (!prevIsStop) continue;

    const bool wordInitial = prev->wordStart;
    const bool stressed = (prev->stress > 0);

    const Token* prevPrev = prevNonSilence(tokens, i - 1);
    const bool intervocalic = (prevPrev && tokIsVowel(*prevPrev) && next && tokIsVowel(*next));

    const bool wordFinal = (!next) || (next && next->wordStart);

    double s = 1.0;
    if (wordInitial && stressed) s = lp.positionalAllophonesStopAspirationWordInitialStressed;
    else if (wordInitial) s = lp.positionalAllophonesStopAspirationWordInitial;
    else if (wordFinal) s = lp.positionalAllophonesStopAspirationWordFinal;
    else if (intervocalic) s = lp.positionalAllophonesStopAspirationIntervocalic;

    s = clampDouble(s, 0.05, 2.0);

    t.durationMs *= s;
    t.fadeMs *= s;
    clampFadeToDuration(t);

    // Also scale aspiration/frication energy if the fields exist.
    if (hasField(t, FieldId::aspirationAmplitude)) {
      setField(t, FieldId::aspirationAmplitude, getField(t, FieldId::aspirationAmplitude) * s);
    }
    if (hasField(t, FieldId::fricationAmplitude)) {
      setField(t, FieldId::fricationAmplitude, getField(t, FieldId::fricationAmplitude) * s);
    }

    // Keep a small fade to avoid clicks if we shortened a lot.
    const double minFade = std::min(t.durationMs, 2.0 / sp);
    if (t.fadeMs < minFade) t.fadeMs = minFade;
    clampFadeToDuration(t);
  }

  // 2) /l/ darkness by position (simple F2 pull)
  for (int i = 0; i < static_cast<int>(tokens.size()); ++i) {
    Token& t = tokens[i];
    if (t.silence || !t.def) continue;

    const std::u32string& key = t.def->key;
    if (!(key == U"l" || key == U"ɫ")) continue;

    const Token* prev = prevNonSilence(tokens, i);
    const Token* next = nextNonSilence(tokens, i);

    const bool preVocalic = (next && tokIsVowel(*next));
    const bool postVocalic = (prev && tokIsVowel(*prev));
    const bool syllabic = (!preVocalic && !postVocalic);

    double d = 0.0;
    if (preVocalic) d = lp.positionalAllophonesLateralDarknessPreVocalic;
    else if (postVocalic) d = lp.positionalAllophonesLateralDarknessPostVocalic;
    else if (syllabic) d = lp.positionalAllophonesLateralDarknessSyllabic;

    d = clamp01(d);
    if (d <= 0.0) continue;

    const double targetF2 = std::max(200.0, lp.positionalAllophonesLateralDarkF2TargetHz);

    if (hasField(t, FieldId::cf2)) {
      const double f2 = getField(t, FieldId::cf2);
      if (f2 > 0.0) setField(t, FieldId::cf2, f2 + (targetF2 - f2) * d);
    }
    if (hasField(t, FieldId::pf2)) {
      const double f2 = getField(t, FieldId::pf2);
      if (f2 > 0.0) setField(t, FieldId::pf2, f2 + (targetF2 - f2) * d);
    }
  }

  // 3) Glottal reinforcement for word-final stops (optional insertion of /ʔ/)
  if (lp.positionalAllophonesGlottalReinforcementEnabled) {
    const PhonemeDef* glott = findPhoneme(ctx.pack, U"ʔ");
    if (glott) {
      const bool ctxHashHash = containsString(lp.positionalAllophonesGlottalReinforcementContexts, "#_#");
      const bool ctxVHash = containsString(lp.positionalAllophonesGlottalReinforcementContexts, "V_#");

      std::vector<Token> out;
      out.reserve(tokens.size() + tokens.size() / 16);

      for (int i = 0; i < static_cast<int>(tokens.size()); ++i) {
        Token t = tokens[i];

        bool shouldInsert = false;
        if (t.def && !t.silence && tokIsStopLike(t) && !tokIsVoiced(t)) {
          if (isWordFinalIndex(tokens, i)) {
            const Token* prev = prevNonSilence(tokens, i);
            const bool prevIsVowel = (prev && tokIsVowel(*prev));
            if ((ctxVHash && prevIsVowel) || ctxHashHash) {
              shouldInsert = true;
            }
          }
        }

        if (shouldInsert) {
          // Avoid double-inserting if the previous token we emitted is already /ʔ/.
          if (!out.empty() && out.back().def && out.back().def->key == U"ʔ") {
            // already there
          } else {
            Token g;
            g.def = glott;
            g.silence = false;
            g.wordStart = false;
            g.syllableStart = false;
            g.stress = 0;
            g.tone.clear();
            g.lengthened = false;
            g.tiedTo = false;
            g.tiedFrom = false;

            g.setMask = glott->setMask;
            for (int k = 0; k < kFrameFieldCount; ++k) {
              g.field[k] = glott->field[k];
            }

            const double baseDur = std::max(6.0, lp.positionalAllophonesGlottalReinforcementDurationMs);
            g.durationMs = baseDur / sp;
            g.fadeMs = std::min(g.durationMs, 3.0 / sp);
            clampFadeToDuration(g);

            out.push_back(g);
          }
        }

        out.push_back(t);
      }

      tokens.swap(out);
    }
  }

  return true;
}

} // namespace nvsp_frontend::passes
