/*
TGSpeechBox — Prosody pass (phrase-final lengthening).
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "prosody.h"

#include <algorithm>

namespace nvsp_frontend::passes {

namespace {

static inline bool isVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

// Find the last vowel token between [start, end) indices. Returns -1 if none.
static int findLastVowel(const std::vector<Token>& tokens, int start, int end) {
  for (int i = end - 1; i >= start; --i) {
    const Token& t = tokens[static_cast<size_t>(i)];
    if (t.silence || !t.def) continue;
    if (isVowel(t)) return i;
  }
  return -1;
}

}  // namespace

bool runProsody(PassContext& ctx, std::vector<Token>& tokens, std::string& outError) {
  (void)outError;

  const auto& lang = ctx.pack.lang;
  if (!lang.phraseFinalLengtheningEnabled) return true;

  if (tokens.empty()) return true;

  // Locate last and penultimate syllable starts.
  int lastSyllStart = -1;
  for (int i = static_cast<int>(tokens.size()) - 1; i >= 0; --i) {
    const Token& t = tokens[static_cast<size_t>(i)];
    if (t.silence || !t.def) continue;
    if (t.syllableStart) {
      lastSyllStart = i;
      break;
    }
  }
  if (lastSyllStart < 0) return true;

  int penultSyllStart = -1;
  for (int i = lastSyllStart - 1; i >= 0; --i) {
    const Token& t = tokens[static_cast<size_t>(i)];
    if (t.silence || !t.def) continue;
    if (t.syllableStart) {
      penultSyllStart = i;
      break;
    }
  }

  // Clause-type scaling.
  double clauseScale = 1.0;
  if (ctx.clauseType == '?') {
    clauseScale = lang.phraseFinalLengtheningQuestionScale;
  } else {
    clauseScale = lang.phraseFinalLengtheningStatementScale;
  }

  const double lastScale = std::max(0.1, lang.phraseFinalLengtheningFinalSyllableScale) * clauseScale;
  const double penultScale = std::max(0.1, lang.phraseFinalLengtheningPenultimateSyllableScale);

  // Apply to nucleus vowel(s) by default, because stretching consonants can smear clarity.
  if (lang.phraseFinalLengtheningNucleusOnlyMode) {
    const int lastNucleus = findLastVowel(tokens, lastSyllStart, static_cast<int>(tokens.size()));
    if (lastNucleus >= 0) {
      Token& v = tokens[static_cast<size_t>(lastNucleus)];
      v.durationMs *= lastScale;
    }

    if (penultSyllStart >= 0) {
      const int penultNucleus = findLastVowel(tokens, penultSyllStart, lastSyllStart);
      if (penultNucleus >= 0) {
        Token& v = tokens[static_cast<size_t>(penultNucleus)];
        v.durationMs *= penultScale;
      }
    }
  } else {
    // Full-syllable mode: scale everything from the last syllable start onward.
    for (size_t i = static_cast<size_t>(lastSyllStart); i < tokens.size(); ++i) {
      Token& t = tokens[i];
      if (t.silence || !t.def) continue;
      t.durationMs *= lastScale;
    }

    if (penultSyllStart >= 0) {
      for (int i = penultSyllStart; i < lastSyllStart; ++i) {
        Token& t = tokens[static_cast<size_t>(i)];
        if (t.silence || !t.def) continue;
        t.durationMs *= penultScale;
      }
    }
  }

  return true;
}

}  // namespace nvsp_frontend::passes
