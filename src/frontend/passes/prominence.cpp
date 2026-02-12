#include "prominence.h"

#include <algorithm>
#include <cmath>

namespace nvsp_frontend::passes {

namespace {

static inline bool isVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool isSilenceOrMissing(const Token& t) {
  return t.silence || !t.def;
}

}  // namespace

bool runProminence(PassContext& ctx, std::vector<Token>& tokens, std::string& outError) {
  (void)outError;

  const auto& lang = ctx.pack.lang;
  if (!lang.prominenceEnabled) return true;
  if (tokens.empty()) return true;

  const double primaryW   = lang.prominencePrimaryStressWeight;
  const double secondaryW = lang.prominenceSecondaryStressWeight;
  const double longVowelW = lang.prominenceLongVowelWeight;
  const std::string& longVowelMode = lang.prominenceLongVowelMode;
  const double wordInitBoost  = lang.prominenceWordInitialBoost;
  const double wordFinalReduc = lang.prominenceWordFinalReduction;

  // ── Pass 1: Compute raw prominence score for each vowel token ──

  // Build word-boundary info for word-position adjustments.
  struct WordInfo {
    size_t start = 0;
    int lastSyllStart = -1;
  };
  std::vector<WordInfo> words;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].wordStart || (i == 0)) {
      WordInfo wi;
      wi.start = i;
      words.push_back(wi);
    }
  }
  // Fill in lastSyllStart for each word
  for (size_t w = 0; w < words.size(); ++w) {
    size_t wEnd = (w + 1 < words.size()) ? words[w + 1].start : tokens.size();
    int lastSyll = -1;
    for (size_t i = words[w].start; i < wEnd; ++i) {
      if (isSilenceOrMissing(tokens[i])) continue;
      if (tokens[i].syllableStart) lastSyll = static_cast<int>(i);
    }
    words[w].lastSyllStart = lastSyll;
  }

  // Helper: find which word a token index belongs to
  auto wordIndexOf = [&](size_t tokIdx) -> size_t {
    for (size_t w = words.size(); w > 0; --w) {
      if (tokIdx >= words[w - 1].start) return w - 1;
    }
    return 0;
  };

  // ── Compute prominence per vowel ──
  for (size_t i = 0; i < tokens.size(); ++i) {
    Token& t = tokens[i];
    if (isSilenceOrMissing(t)) continue;

    // Only vowels get prominence scores; consonants get 0.0 (neutral).
    if (!isVowel(t)) {
      t.prominence = 0.0;
      continue;
    }

    // Diphthong offglides: inherit prominence from the preceding nucleus.
    // Without this, /ɪ/ in /aɪ/ gets scored 0.0 (unstressed) and receives
    // amplitude reduction, creating a 2-beat artifact instead of a smooth glide.
    if (t.tiedFrom) {
      // Walk backward to find the tied-to nucleus.
      for (size_t j = i; j > 0; --j) {
        const Token& prev = tokens[j - 1];
        if (isSilenceOrMissing(prev)) continue;
        if (isVowel(prev) && prev.tiedTo) {
          t.prominence = prev.prominence;  // may still be -1.0 if not scored yet
          break;
        }
        break;  // no tied-to found immediately before, give up
      }
      if (t.prominence < 0.0) t.prominence = 0.5;  // safe fallback: neutral
      continue;
    }

    double score = 0.0;

    // Source 1: Stress marks
    if (t.stress == 1) {
      score = std::max(score, primaryW);
    } else if (t.stress == 2) {
      score = std::max(score, secondaryW);
    } else {
      // Vowel might inherit stress from syllable start (eSpeak puts
      // stress on syllable-initial consonant, not vowel). Walk backward
      // to the syllable start to check.
      for (size_t j = i; j > 0; --j) {
        const Token& prev = tokens[j - 1];
        if (prev.syllableStart) {
          if (prev.stress == 1) score = std::max(score, primaryW);
          else if (prev.stress == 2) score = std::max(score, secondaryW);
          break;
        }
        if (prev.wordStart) break;  // don't cross word boundaries
        if (isSilenceOrMissing(prev)) continue;
        if (isVowel(prev)) break;  // hit another vowel = different syllable
      }
    }

    // Source 2: Vowel length (ː)
    if (t.lengthened > 0 && longVowelMode != "never") {
      bool apply = false;
      if (longVowelMode == "always") {
        apply = true;
      } else {
        // "unstressed-only" (default): only boost if stress didn't already
        // give this vowel high prominence
        apply = (score < 0.01);  // effectively unstressed
      }
      if (apply) {
        score = std::max(score, longVowelW);
      }
    }

    // Source 3: Word position adjustments
    size_t wIdx = wordIndexOf(i);
    const WordInfo& wi = words[wIdx];

    // Word-initial: is this vowel in the first syllable of the word?
    // Check if there's no earlier vowel in this word.
    if (wordInitBoost > 0.0) {
      bool isFirstVowel = true;
      for (size_t j = wi.start; j < i; ++j) {
        if (!isSilenceOrMissing(tokens[j]) && isVowel(tokens[j])) {
          isFirstVowel = false;
          break;
        }
      }
      if (isFirstVowel) {
        score += wordInitBoost;
      }
    }

    // Word-final: is this vowel in the last syllable of the word?
    if (wordFinalReduc > 0.0 && wi.lastSyllStart >= 0) {
      // Check if this token is at or after the last syllable start
      if (static_cast<int>(i) >= wi.lastSyllStart) {
        score -= wordFinalReduc;
      }
    }

    // Clamp to [0.0, 1.0]
    t.prominence = std::max(0.0, std::min(1.0, score));
  }

  // ── Pass 2: Duration realization ──

  const double floorMs = lang.prominenceDurationProminentFloorMs;
  const double reducedCeil = lang.prominenceDurationReducedCeiling;
  const double speed = ctx.speed;

  for (Token& t : tokens) {
    if (isSilenceOrMissing(t) || !isVowel(t)) continue;
    if (t.prominence < 0.0) continue;  // not set

    // Prominent vowels: enforce duration floor.
    // Skip tiedFrom tokens (diphthong offglides) — their short duration IS the glide.
    if (floorMs > 0.0 && t.prominence >= 0.5 && !t.tiedFrom) {
      double effectiveFloor = floorMs / speed;
      t.durationMs = std::max(t.durationMs, effectiveFloor);
    }

    // Non-prominent vowels: apply reduction ceiling
    if (reducedCeil < 1.0 && t.prominence < 0.3) {
      // Scale linearly: prominence 0.0 → full reduction, 0.3 → no reduction
      double blend = t.prominence / 0.3;
      double scale = reducedCeil + blend * (1.0 - reducedCeil);
      t.durationMs *= scale;
    }
  }

  // ── Pass 3: Amplitude realization ──

  const double boostDb = lang.prominenceAmplitudeBoostDb;
  const double reducDb = lang.prominenceAmplitudeReductionDb;
  const int vaIdx = static_cast<int>(FieldId::voiceAmplitude);

  if (boostDb > 0.0 || reducDb > 0.0) {
    for (Token& t : tokens) {
      if (isSilenceOrMissing(t) || !isVowel(t)) continue;
      if (t.prominence < 0.0) continue;

      double currentAmp = 0.0;
      if (t.setMask & (1ULL << vaIdx)) {
        currentAmp = t.field[vaIdx];
      } else if (t.def) {
        currentAmp = t.def->field[vaIdx];
      }
      if (currentAmp <= 0.0) continue;

      double dbChange = 0.0;
      if (t.prominence >= 0.5 && boostDb > 0.0) {
        // Scale boost by how prominent: 0.5 → half boost, 1.0 → full boost
        double factor = (t.prominence - 0.5) / 0.5;
        dbChange = boostDb * factor;
      } else if (t.prominence < 0.3 && reducDb > 0.0) {
        // Scale reduction by how non-prominent: 0.3 → no reduction, 0.0 → full
        double factor = 1.0 - (t.prominence / 0.3);
        dbChange = -reducDb * factor;
      }

      if (dbChange != 0.0) {
        double linearScale = std::pow(10.0, dbChange / 20.0);
        t.field[vaIdx] = currentAmp * linearScale;
        t.setMask |= (1ULL << vaIdx);
      }
    }
  }

  return true;
}

}  // namespace nvsp_frontend::passes
