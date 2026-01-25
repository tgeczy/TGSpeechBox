#include "microprosody.h"

#include <algorithm>

namespace nvsp_frontend::passes {

namespace {

static inline bool isVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool isVoicelessConsonant(const Token& t) {
  if (!t.def) return false;
  if ((t.def->flags & kIsVowel) != 0) return false;
  // This codebase only tracks "voiced" as an explicit flag.
  // Treat any non-vowel without kIsVoiced as voiceless.
  return (t.def->flags & kIsVoiced) == 0;
}

static inline bool isVoicedStop(const Token& t) {
  if (!t.def) return false;
  if ((t.def->flags & kIsStop) == 0) return false;
  return (t.def->flags & kIsVoiced) != 0;
}

static inline bool isSilenceOrMissing(const Token& t) {
  return t.silence || !t.def;
}

static inline bool hasField(const Token& t, FieldId id) {
  return (t.setMask & (1ULL << static_cast<int>(id))) != 0;
}

}  // namespace

bool runMicroprosody(PassContext& ctx, std::vector<Token>& tokens, std::string& outError) {
  (void)outError;

  const auto& lang = ctx.pack.lang;
  if (!lang.microprosodyEnabled) return true;

  const int vpIdx = static_cast<int>(FieldId::voicePitch);
  const int epIdx = static_cast<int>(FieldId::endVoicePitch);

  for (size_t i = 0; i < tokens.size(); ++i) {
    Token& v = tokens[i];
    if (!isVowel(v) || isSilenceOrMissing(v)) continue;

    // Find the nearest previous real token.
    const Token* prev = nullptr;
    for (size_t j = i; j > 0; --j) {
      const Token& cand = tokens[j - 1];
      if (isSilenceOrMissing(cand)) continue;
      prev = &cand;
      break;
    }
    if (!prev) continue;

    if (!hasField(v, FieldId::voicePitch) || !hasField(v, FieldId::endVoicePitch)) {
      continue;
    }

    double startPitch = v.field[vpIdx];
    double endPitch = v.field[epIdx];

    // Voiceless consonants tend to raise the following vowel onset.
    if (lang.microprosodyVoicelessF0RaiseEnabled && isVoicelessConsonant(*prev)) {
      const double d = lang.microprosodyVoicelessF0RaiseHz;
      v.field[vpIdx] = std::max(20.0, startPitch + d);
      // Let it decay naturally by keeping endPitch unchanged.
      v.field[epIdx] = std::max(20.0, endPitch);
      continue;
    }

    // Voiced stop consonants can slightly lower the following vowel onset.
    if (lang.microprosodyVoicedF0LowerEnabled && isVoicedStop(*prev)) {
      const double d = lang.microprosodyVoicedF0LowerHz;
      v.field[vpIdx] = std::max(20.0, startPitch + d);
      v.field[epIdx] = std::max(20.0, endPitch);
      continue;
    }
  }

  return true;
}

}  // namespace nvsp_frontend::passes
