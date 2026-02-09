// =============================================================================
// Cluster Timing Pass - context-sensitive consonant duration adjustment
// =============================================================================
//
// Shortens consonants that occur in clusters (adjacent to other consonants)
// and adjusts word-medial/word-final obstruent durations.

#include "cluster_timing.h"
#include "../pack.h"
#include "../ipa_engine.h"

#include <algorithm>
#include <cmath>

namespace nvsp_frontend::passes {

namespace {

static inline bool isSilence(const Token& t) {
  return t.silence || !t.def;
}

static inline bool isVowelFlag(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool isStop(const Token& t) {
  return t.def && ((t.def->flags & kIsStop) != 0);
}

static inline bool isAffricate(const Token& t) {
  return t.def && ((t.def->flags & kIsAfricate) != 0);
}

static inline bool isFricative(const Token& t) {
  if (!t.def) return false;
  if (isStop(t) || isAffricate(t)) return false;
  const int idx = static_cast<int>(FieldId::fricationAmplitude);
  const uint64_t bit = 1ULL << idx;
  double v = 0.0;
  if (t.setMask & bit) v = t.field[idx];
  else if (t.def->setMask & bit) v = t.def->field[idx];
  return v > 0.05;
}

static inline bool isObstruent(const Token& t) {
  return isStop(t) || isAffricate(t) || isFricative(t);
}

static inline bool isConsonant(const Token& t) {
  if (!t.def) return false;
  return (t.def->flags & kIsVowel) == 0;
}

// Find previous non-silence token. Returns -1 if none.
static int findPrevNonSilence(const std::vector<Token>& tokens, int from) {
  for (int j = from - 1; j >= 0; --j) {
    if (!isSilence(tokens[static_cast<size_t>(j)])) return j;
  }
  return -1;
}

// Find next non-silence token. Returns -1 if none.
static int findNextNonSilence(const std::vector<Token>& tokens, int from) {
  for (int j = from + 1; j < static_cast<int>(tokens.size()); ++j) {
    if (!isSilence(tokens[static_cast<size_t>(j)])) return j;
  }
  return -1;
}

}  // namespace

bool runClusterTiming(PassContext& ctx, std::vector<Token>& tokens, std::string& outError) {
  (void)outError;

  const auto& lang = ctx.pack.lang;
  if (!lang.clusterTimingEnabled) return true;

  for (int i = 0; i < static_cast<int>(tokens.size()); ++i) {
    Token& t = tokens[static_cast<size_t>(i)];
    if (isSilence(t)) continue;
    if (!isConsonant(t)) continue;

    const int prevIdx = findPrevNonSilence(tokens, i);
    const int nextIdx = findNextNonSilence(tokens, i);

    const Token* prev = (prevIdx >= 0) ? &tokens[static_cast<size_t>(prevIdx)] : nullptr;
    const Token* next = (nextIdx >= 0) ? &tokens[static_cast<size_t>(nextIdx)] : nullptr;

    const bool prevIsConsonant = prev && !isSilence(*prev) && isConsonant(*prev);
    const bool nextIsConsonant = next && !isSilence(*next) && isConsonant(*next);

    // Determine if we're in a cluster (adjacent non-silence is also consonant).
    const bool inCluster = prevIsConsonant || nextIsConsonant;

    // Word position.
    const bool isWordInitial = t.wordStart;
    const bool isWordFinal = !next || (next && next->wordStart);

    double scale = 1.0;

    if (inCluster) {
      // Triple cluster: both neighbors are consonants.
      if (prevIsConsonant && nextIsConsonant) {
        scale = std::min(scale, lang.clusterTimingTripleClusterMiddleScale);
      } else {
        // Two-consonant cluster: classify by type pair.
        const bool curIsStop = isStop(t) || isAffricate(t);
        const bool curIsFric = isFricative(t);

        if (nextIsConsonant) {
          // Current is first in cluster, next is second.
          const bool nextIsStop = isStop(*next) || isAffricate(*next);
          const bool nextIsFric = isFricative(*next);

          if (curIsFric && nextIsStop) {
            scale = std::min(scale, lang.clusterTimingFricBeforeStopScale);
          } else if (curIsStop && nextIsFric) {
            scale = std::min(scale, lang.clusterTimingStopBeforeFricScale);
          } else if (curIsFric && nextIsFric) {
            scale = std::min(scale, lang.clusterTimingFricBeforeFricScale);
          } else if (curIsStop && nextIsStop) {
            scale = std::min(scale, lang.clusterTimingStopBeforeStopScale);
          }
        }

        if (prevIsConsonant) {
          // Current is second in cluster, prev is first.
          const bool prevIsStop = isStop(*prev) || isAffricate(*prev);
          const bool prevIsFric = isFricative(*prev);

          if (prevIsFric && curIsStop) {
            // We're the stop after a fricative — the fricative already got scaled.
            // Don't double-scale the stop itself.
          } else if (prevIsStop && curIsFric) {
            scale = std::min(scale, lang.clusterTimingStopBeforeFricScale);
          } else if (prevIsFric && curIsFric) {
            scale = std::min(scale, lang.clusterTimingFricBeforeFricScale);
          } else if (prevIsStop && curIsStop) {
            scale = std::min(scale, lang.clusterTimingStopBeforeStopScale);
          }
        }
      }

      // Affricate in cluster gets additional shortening.
      if (isAffricate(t)) {
        scale *= lang.clusterTimingAffricateInClusterScale;
      }
    }

    // Word-medial consonants (not at word boundary, not in a cluster).
    if (!isWordInitial && !isWordFinal && !inCluster) {
      scale = std::min(scale, lang.clusterTimingWordMedialConsonantScale);
    }

    // Word-final obstruents.
    if (isWordFinal && isObstruent(t)) {
      scale = std::min(scale, lang.clusterTimingWordFinalObstruentScale);
    }

    if (scale < 1.0) {
      t.durationMs *= scale;
      if (t.durationMs < 2.0) t.durationMs = 2.0;  // floor
      if (t.fadeMs > t.durationMs) t.fadeMs = t.durationMs;
    }
  }

  return true;
}

}  // namespace nvsp_frontend::passes
