/*
TGSpeechBox — Prominence pass (stress scoring and duration/amplitude realization).
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

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

  // Realization parameters (used by passes 2 and 3).
  const double primaryW   = lang.prominencePrimaryStressWeight;
  const double secondaryW = lang.prominenceSecondaryStressWeight;

  // Score settings (phonological classification).
  const double secondaryLevel = lang.prominenceSecondaryStressLevel;
  const double longVowelLevel = lang.prominenceLongVowelWeight;
  const std::string& longVowelMode = lang.prominenceLongVowelMode;
  const double wordInitBoost  = lang.prominenceWordInitialBoost;
  const double wordFinalReduc = lang.prominenceWordFinalReduction;

  // ── Pass 1: Compute raw prominence score for each vowel token ──
  //
  // Score reflects phonological stress category:
  //   primary stress   → 1.0
  //   secondary stress → secondaryLevel (default 0.6)
  //   unstressed       → 0.0
  // Plus additive word-position tweaks.  Clamped to [0, 1].

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

    // Source 1: Stress marks → categorical level
    if (t.stress == 1) {
      score = 1.0;
    } else if (t.stress == 2) {
      score = secondaryLevel;
    } else {
      // Vowel might inherit stress from syllable start (eSpeak puts
      // stress on syllable-initial consonant, not vowel). Walk backward
      // to the syllable start to check.
      for (size_t j = i; j > 0; --j) {
        const Token& prev = tokens[j - 1];
        if (prev.syllableStart) {
          if (prev.stress == 1) score = 1.0;
          else if (prev.stress == 2) score = secondaryLevel;
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
        score = std::max(score, longVowelLevel);
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

  // ── Pass 1b: Monosyllable prominence floor ──
  //
  // Content monosyllables ("box", "cat", "top") are always prominent
  // in English even when eSpeak omits the stress mark. Without this,
  // they score 0.0 and hit the reducedCeiling penalty, making them
  // sound clipped and sharp.
  //
  // Heuristic: if a word contains exactly one vowel and that vowel's
  // prominence is below secondaryLevel, boost it to secondaryLevel.
  // This prevents reduction without over-promoting — the vowel gets
  // secondary-stress treatment (adequate duration) rather than primary.

  const double monoFloor = secondaryLevel;  // 0.6 by default

  for (size_t w = 0; w < words.size(); ++w) {
    const size_t wStart = words[w].start;
    const size_t wEnd   = (w + 1 < words.size()) ? words[w + 1].start : tokens.size();

    // Count vowels and find the single vowel if monosyllabic.
    int vowelCount = 0;
    int monoVowelIdx = -1;
    for (size_t i = wStart; i < wEnd; ++i) {
      if (isSilenceOrMissing(tokens[i])) continue;
      if (tokens[i].tiedFrom) continue;  // don't count diphthong offglides
      if (isVowel(tokens[i])) {
        vowelCount++;
        monoVowelIdx = static_cast<int>(i);
      }
    }

    if (vowelCount == 1 && monoVowelIdx >= 0) {
      Token& v = tokens[monoVowelIdx];
      if (v.prominence >= 0.0 && v.prominence < monoFloor) {
        v.prominence = monoFloor;
      }
    }
  }

  // ── Pass 1c: Full-vowel protection ──
  //
  // In English, full vowels (not schwa/reduced-ɪ) are almost never
  // truly unstressed. When eSpeak omits secondary stress on compound
  // word second elements ("Firefox", "laptop", "desktop"), the full
  // vowel should not be reduced. Boost it to a minimum floor so it
  // avoids the reducedCeiling penalty and gets the duration floor.

  const double fullVowelFloor = lang.prominenceFullVowelFloor;
  if (fullVowelFloor > 0.0) {
    for (size_t i = 0; i < tokens.size(); ++i) {
      Token& t = tokens[i];
      if (isSilenceOrMissing(t) || !isVowel(t)) continue;
      if (t.tiedFrom) continue;
      if (t.prominence < 0.0) continue;
      if (t.prominence >= fullVowelFloor) continue;

      // Reduced vowels that genuinely deserve low prominence.
      // Everything NOT in this list is considered "full" and gets
      // the floor applied.
      bool isReduced = false;
      if (t.baseChar != 0) {
        switch (t.baseChar) {
          case U'\u0259':  // ə  schwa
          case U'\u0250':  // ɐ  near-open central
          case U'\u1D4A':  // ᵊ  modifier schwa
          case U'\u0268':  // ɨ  barred-i
          case U'\u1D7B':  // ᵻ  barred-ɪ
            isReduced = true;
            break;
          default:
            break;
        }
      }

      if (!isReduced) {
        t.prominence = fullVowelFloor;
      }
    }
  }

  // ── Pass 2: Duration realization ──
  //
  // Threshold-based vowel duration scaling:
  //   prominence >= 0.9 → primary stress   → multiply by primaryStressWeight
  //   prominence >= 0.4 → secondary stress → multiply by secondaryStressWeight
  //   prominence <  0.3 → unstressed       → apply reducedCeiling
  //   prominence >= 0.4 → apply floor (safety net)

  const double floorMs        = lang.prominenceDurationProminentFloorMs;
  const double primaryFloorMs = lang.prominenceDurationPrimaryFloorMs;
  const double reducedCeil    = lang.prominenceDurationReducedCeiling;
  const double speed          = ctx.speed;

  for (Token& t : tokens) {
    if (isSilenceOrMissing(t) || !isVowel(t)) continue;
    if (t.prominence < 0.0) continue;  // not set

    // Skip tiedFrom tokens (diphthong offglides) — their short duration IS the glide.
    if (t.tiedFrom) continue;

    // Stress-based duration scaling (replaces old primaryStressDiv).
    if (t.prominence >= 0.9) {
      t.durationMs *= primaryW;
    } else if (t.prominence >= 0.4) {
      t.durationMs *= secondaryW;
    }

    // Primary stress floor — prevents short monophthongs like /ɒ/ in
    // "box" from sounding clipped. Skips diphthong nuclei (tiedTo) since
    // they already have the offglide adding perceived duration.
    if (t.prominence >= 0.9 && primaryFloorMs > 0.0 && !t.tiedTo) {
      double effectivePFloor = primaryFloorMs / speed;
      t.durationMs = std::max(t.durationMs, effectivePFloor);
    }

    // Safety floor for prominent vowels
    if (t.prominence >= 0.4 && floorMs > 0.0) {
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

  // ── Pass 2b: Syllable-position duration shaping ──
  //
  // Onset consonants get slightly more time (they initiate the gesture),
  // coda consonants get less (they trail off).  Unstressed open syllables
  // (no coda) compress their nucleus — these are the lightest syllables
  // in natural speech rhythm.

  if (lang.syllableDurationEnabled) {
    const double onsetSc = lang.syllableDurationOnsetScale;
    const double codaSc  = lang.syllableDurationCodaScale;
    const double openSc  = lang.syllableDurationUnstressedOpenNucleusScale;

    for (size_t w = 0; w < words.size(); ++w) {
      const size_t wStart = words[w].start;
      const size_t wEnd   = (w + 1 < words.size()) ? words[w + 1].start : tokens.size();

      // Count syllables in this word.
      int maxSyll = -1;
      for (size_t i = wStart; i < wEnd; ++i) {
        if (tokens[i].syllableIndex > maxSyll) maxSyll = tokens[i].syllableIndex;
      }
      if (maxSyll < 1) continue;  // monosyllable or unassigned — skip

      // Process each syllable except the last — word-final syllables are
      // already shaped by wordFinalObstruentScale and phrase-final lengthening.
      // Compressing them further makes final syllables disappear.
      for (int syll = 0; syll < maxSyll; ++syll) {
        // Collect real phoneme indices in this syllable.
        int nucleusIdx = -1;
        bool syllStressed = false;
        bool hasCoda = false;

        // First pass: find nucleus and stress.
        for (size_t i = wStart; i < wEnd; ++i) {
          Token& t = tokens[i];
          if (t.syllableIndex != syll) continue;
          if (isSilenceOrMissing(t)) continue;
          if (t.preStopGap || t.clusterGap || t.vowelHiatusGap ||
              t.postStopAspiration || t.voicedClosure) continue;
          if (t.stress > 0) syllStressed = true;
          if (nucleusIdx < 0 && isVowel(t)) nucleusIdx = static_cast<int>(i);
        }
        if (nucleusIdx < 0) continue;  // no vowel — skip

        // Check for coda consonants (real phonemes after nucleus).
        for (size_t i = static_cast<size_t>(nucleusIdx) + 1; i < wEnd; ++i) {
          Token& t = tokens[i];
          if (t.syllableIndex != syll) break;
          if (isSilenceOrMissing(t)) continue;
          if (t.preStopGap || t.clusterGap || t.vowelHiatusGap ||
              t.postStopAspiration || t.voicedClosure) continue;
          if (!isVowel(t)) { hasCoda = true; break; }
        }

        // Apply scales.
        for (size_t i = wStart; i < wEnd; ++i) {
          Token& t = tokens[i];
          if (t.syllableIndex != syll) continue;
          if (isSilenceOrMissing(t)) continue;
          if (t.preStopGap || t.clusterGap || t.vowelHiatusGap ||
              t.postStopAspiration || t.voicedClosure) continue;

          if (!isVowel(t)) {
            // Consonant: onset or coda?
            if (static_cast<int>(i) < nucleusIdx) {
              t.durationMs *= onsetSc;
            } else if (static_cast<int>(i) > nucleusIdx) {
              t.durationMs *= codaSc;
            }
          } else if (!syllStressed && !hasCoda && !t.tiedFrom) {
            // Unstressed open-syllable vowel.
            t.durationMs *= openSc;
          }

          // Safety clamps.
          if (t.durationMs < 2.0) t.durationMs = 2.0;
          t.fadeMs = std::min(t.fadeMs, t.durationMs);
        }
      }
    }
  }

  // ── Pass 3: Amplitude realization ──
  //
  // Boost is scaled by primaryStressWeight so the weight knob controls
  // how much stressed vowels stand out.  Reduction is NOT scaled by
  // the weight — unstressed vowels get reduced regardless.

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
        // Scale boost by prominence level AND stress weight
        double factor = (t.prominence - 0.5) / 0.5;
        dbChange = boostDb * primaryW * factor;
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
