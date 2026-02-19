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

static inline bool isSemivowel(const Token& t) {
  return t.def && ((t.def->flags & kIsSemivowel) != 0);
}

// Find the first vowel token between [start, end) indices.  Returns -1 if none.
static int findFirstVowel(const std::vector<Token>& tokens, int start, int end) {
  for (int i = start; i < end; ++i) {
    const Token& t = tokens[static_cast<size_t>(i)];
    if (t.silence || !t.def) continue;
    if (isVowel(t)) return i;
  }
  return -1;
}

// Extend the nucleus past adjacent vowels (diphthong: /eɪ/) and semivowels
// (offglide: /ej/ from non-eSpeak phonemizers).  First non-vowel,
// non-semivowel token marks the start of the true coda.
static int findNucleusEnd(const std::vector<Token>& tokens, int nucleusIdx, int end) {
  int last = nucleusIdx;
  for (int i = nucleusIdx + 1; i < end; ++i) {
    const Token& t = tokens[static_cast<size_t>(i)];
    if (t.silence || !t.def) continue;
    if (isVowel(t) || isSemivowel(t)) {
      last = i;  // diphthong vowel or offglide — still nucleus
    } else {
      break;     // real coda consonant — stop
    }
  }
  return last;
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
      const bool isDiph = tokens[static_cast<size_t>(lastNucleus)].tiedTo;
      const double nucScaleOnly = isDiph
          ? ((lang.phraseFinalLengtheningNucleusDiphthongScale > 0.0)
              ? lang.phraseFinalLengtheningNucleusDiphthongScale * clauseScale
              : lastScale)
          : lastScale;
      Token& v = tokens[static_cast<size_t>(lastNucleus)];
      v.durationMs *= nucScaleOnly;
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
    // With coda bias: nucleus gets nucleusScale, post-nucleus coda gets codaScale.
    const bool codaBias = lang.phraseFinalLengtheningCodaScale > 0.0 ||
                          lang.phraseFinalLengtheningNucleusScale > 0.0;
    const int lastNucleus = codaBias
        ? findFirstVowel(tokens, lastSyllStart, static_cast<int>(tokens.size()))
        : -1;
    const double nucScaleMono = (lang.phraseFinalLengtheningNucleusScale > 0.0)
        ? lang.phraseFinalLengtheningNucleusScale * clauseScale : lastScale;
    const double nucScaleDiph = (lang.phraseFinalLengtheningNucleusDiphthongScale > 0.0)
        ? lang.phraseFinalLengtheningNucleusDiphthongScale * clauseScale : nucScaleMono;

    // Detect diphthong: check if the nucleus vowel ties to an offglide.
    const bool isDiphthongNucleus = (lastNucleus >= 0)
        && tokens[static_cast<size_t>(lastNucleus)].tiedTo;
    const double nucScale = isDiphthongNucleus ? nucScaleDiph : nucScaleMono;
    const double codScaleGeneric = (lang.phraseFinalLengtheningCodaScale > 0.0)
        ? lang.phraseFinalLengtheningCodaScale * clauseScale : lastScale;
    const double codScaleStop = (lang.phraseFinalLengtheningCodaStopScale > 0.0)
        ? lang.phraseFinalLengtheningCodaStopScale * clauseScale : codScaleGeneric;
    const double codScaleFric = (lang.phraseFinalLengtheningCodaFricativeScale > 0.0)
        ? lang.phraseFinalLengtheningCodaFricativeScale * clauseScale : codScaleGeneric;

    // Find where nucleus ends (including diphthong offglides).
    const int nucleusEnd = (codaBias && lastNucleus >= 0)
        ? findNucleusEnd(tokens, lastNucleus, static_cast<int>(tokens.size()))
        : lastNucleus;

    for (size_t i = static_cast<size_t>(lastSyllStart); i < tokens.size(); ++i) {
      Token& t = tokens[i];
      if (t.silence || !t.def) continue;
      if (codaBias && lastNucleus >= 0) {
        const int ii = static_cast<int>(i);
        if (ii >= lastNucleus && ii <= nucleusEnd) {
          t.durationMs *= nucScale;      // vowel + offglide = nucleus
        } else if (ii > nucleusEnd) {
          // Class-aware coda scaling: stops get room for burst,
          // fricatives get minimal stretch to avoid hissing.
          if (t.def->flags & kIsStop)
            t.durationMs *= codScaleStop;
          else if (t.def->field[static_cast<int>(FieldId::fricationAmplitude)] > 0.0)
            t.durationMs *= codScaleFric;
          else
            t.durationMs *= codScaleGeneric;
        } else {
          t.durationMs *= lastScale;     // onset
        }
      } else {
        t.durationMs *= lastScale;
      }
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
