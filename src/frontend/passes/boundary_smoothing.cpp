#include "boundary_smoothing.h"

#include <algorithm>

namespace nvsp_frontend::passes {

namespace {

static inline bool tokIsSilenceOrMissing(const Token& t) {
  return t.silence || !t.def;
}

static inline bool tokIsVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool tokIsSemivowel(const Token& t) {
  return t.def && ((t.def->flags & kIsSemivowel) != 0);
}

static inline bool tokIsVowelLike(const Token& t) {
  return tokIsVowel(t) || tokIsSemivowel(t);
}

static inline bool tokIsStop(const Token& t) {
  if (!t.def || t.silence) return false;
  return (t.def->flags & kIsStop) != 0;
}

static inline bool tokIsAffricate(const Token& t) {
  if (!t.def || t.silence) return false;
  return (t.def->flags & kIsAfricate) != 0;
}

static inline bool tokIsStopLike(const Token& t) {
  if (!t.def || t.silence) return false;
  if (t.postStopAspiration) return true;
  const uint32_t f = t.def->flags;
  return ((f & kIsStop) != 0) || ((f & kIsAfricate) != 0);
}

static inline bool tokIsNasal(const Token& t) {
  if (!t.def || t.silence) return false;
  return (t.def->flags & kIsNasal) != 0;
}

static inline bool tokIsLiquid(const Token& t) {
  if (!t.def || t.silence) return false;
  return (t.def->flags & kIsLiquid) != 0;
}

static inline bool tokIsFricative(const Token& t) {
  if (!t.def || t.silence) return false;
  // Check fricationAmplitude > 0 and not a stop/affricate
  const int fa = static_cast<int>(FieldId::fricationAmplitude);
  const uint64_t bit = 1ULL << fa;
  double v = 0.0;
  if (t.setMask & bit) v = t.field[fa];
  else if (t.def->setMask & bit) v = t.def->field[fa];
  if (v <= 0.0) return false;
  // Exclude stops and affricates (they have frication but aren't "fricatives")
  const uint32_t f = t.def->flags;
  if ((f & kIsStop) != 0) return false;
  if ((f & kIsAfricate) != 0) return false;
  return true;
}

static inline bool tokIsVoiced(const Token& t) {
  return t.def && ((t.def->flags & kIsVoiced) != 0);
}

static inline void clampFadeToDuration(Token& t) {
  if (t.durationMs < 0.0) t.durationMs = 0.0;
  if (t.fadeMs < 0.0) t.fadeMs = 0.0;
  if (t.fadeMs > t.durationMs) t.fadeMs = t.durationMs;
}

// Find the previous real (non-silence) token, skipping micro-gaps.
static int findPrevReal(
    const std::vector<Token>& tokens,
    int idxBefore,
    double maxSkipSilenceMs) {
  for (int j = idxBefore; j >= 0; --j) {
    const Token& t = tokens[static_cast<size_t>(j)];

    if (!tokIsSilenceOrMissing(t)) return j;

    if (t.silence) {
      const bool isMicroGap = t.preStopGap || t.clusterGap || t.vowelHiatusGap;
      if (!isMicroGap && t.durationMs > maxSkipSilenceMs) {
        break;
      }
    }
  }
  return -1;
}

}  // namespace

bool runBoundarySmoothing(PassContext& ctx, std::vector<Token>& tokens, std::string& outError) {
  (void)outError;

  const auto& lang = ctx.pack.lang;
  if (!lang.boundarySmoothingEnabled) return true;
  if (tokens.size() < 2) return true;

  const double sp = (ctx.speed > 0.0) ? ctx.speed : 1.0;
  
  // Fade values tuned for audible smoothing without "doubling" artifacts
  // These are noticeably smoother than defaults but still crisp
  const double v2sEff = 22.0 / sp;   // Vowel -> Stop
  const double s2vEff = 20.0 / sp;   // Stop -> Vowel
  const double v2fEff = 18.0 / sp;   // Vowel -> Fricative
  
  // Derived transition fades
  const double f2v = 18.0 / sp;      // Fricative -> Vowel
  const double v2n = 16.0 / sp;      // Vowel -> Nasal
  const double n2v = 16.0 / sp;      // Nasal -> Vowel
  const double v2l = 14.0 / sp;      // Vowel -> Liquid
  const double l2v = 14.0 / sp;      // Liquid -> Vowel
  const double n2s = 12.0 / sp;      // Nasal -> Stop (cluster)
  const double l2s = 12.0 / sp;      // Liquid -> Stop (cluster)
  const double f2s = 10.0 / sp;      // Fricative -> Stop (cluster)
  const double s2f = 14.0 / sp;      // Stop -> Fricative
  const double v2v = 18.0 / sp;      // Vowel -> Vowel (hiatus)

  // Maximum fade as fraction of token duration (preserve steady-state).
  // 0.75 allows short phones to be mostly transition (they have no
  // meaningful steady-state anyway) while still reserving 25% hold.
  constexpr double kMaxFadeRatio = 0.75;

  // Minimum fade floor (ms).  Prevents the ratio cap from creating
  // near-discontinuities on very short sentence-final phones.
  constexpr double kMinFadeMs = 6.0;
  
  // If there's a real pause, don't treat earlier phonemes as adjacent.
  const double maxSkipSilenceMs = 60.0;

  for (int i = 0; i < static_cast<int>(tokens.size()); ++i) {
    Token& cur = tokens[static_cast<size_t>(i)];
    if (tokIsSilenceOrMissing(cur)) continue;

    const int prevIdx = findPrevReal(tokens, i - 1, maxSkipSilenceMs);
    if (prevIdx < 0) continue;
    const Token& prev = tokens[static_cast<size_t>(prevIdx)];

    double targetFade = 0.0;

    // Determine transition type and target fade
    const bool prevVowelLike = tokIsVowelLike(prev);
    const bool curVowelLike = tokIsVowelLike(cur);
    const bool prevStop = tokIsStopLike(prev);
    const bool curStop = tokIsStopLike(cur);
    const bool prevFric = tokIsFricative(prev);
    const bool curFric = tokIsFricative(cur);
    const bool prevNasal = tokIsNasal(prev);
    const bool curNasal = tokIsNasal(cur);
    const bool prevLiquid = tokIsLiquid(prev);
    const bool curLiquid = tokIsLiquid(cur);

    // === VOWEL TRANSITIONS ===
    if (prevVowelLike && curStop) {
      targetFade = v2sEff;  // Vowel -> Stop
    } else if (prevStop && curVowelLike) {
      targetFade = s2vEff;  // Stop -> Vowel
    } else if (prevVowelLike && curFric) {
      targetFade = v2fEff;  // Vowel -> Fricative
    } else if (prevFric && curVowelLike) {
      targetFade = f2v;  // Fricative -> Vowel
    } else if (prevVowelLike && curNasal) {
      targetFade = v2n;  // Vowel -> Nasal
    } else if (prevNasal && curVowelLike) {
      targetFade = n2v;  // Nasal -> Vowel
    } else if (prevVowelLike && curLiquid) {
      targetFade = v2l;  // Vowel -> Liquid
    } else if (prevLiquid && curVowelLike) {
      targetFade = l2v;  // Liquid -> Vowel
    } else if (prevVowelLike && curVowelLike && !cur.tiedFrom) {
      targetFade = v2v;  // Vowel -> Vowel (hiatus, but not tied diphthongs)
    }
    // === CONSONANT CLUSTER TRANSITIONS ===
    else if (prevNasal && curStop) {
      targetFade = n2s;  // Nasal -> Stop (e.g., "nt", "mp")
    } else if (prevLiquid && curStop) {
      targetFade = l2s;  // Liquid -> Stop (e.g., "lt", "rp")
    } else if (prevFric && curStop) {
      targetFade = f2s;  // Fricative -> Stop (e.g., "st", "sp")
    } else if (prevStop && curFric) {
      targetFade = s2f;  // Stop -> Fricative (e.g., "ts" release)
    }
    // === Missing consonant-to-consonant transitions ===
    else if (prevNasal && curFric) {
      targetFade = n2s;  // Nasal -> Fricative (e.g. "nh" in "enhance")
    } else if (prevFric && curNasal) {
      targetFade = n2s;  // Fricative -> Nasal
    } else if (prevStop && curNasal) {
      targetFade = n2s;  // Stop -> Nasal
    } else if (prevNasal && curLiquid) {
      targetFade = n2s;  // Nasal -> Liquid
    } else if (prevLiquid && curFric) {
      targetFade = l2s;  // Liquid -> Fricative
    }
    // === FALLBACK: any real consonant -> any real consonant not yet covered ===
    else if (!prevVowelLike && !curVowelLike &&
             !tokIsSilenceOrMissing(prev) && !tokIsSilenceOrMissing(cur)) {
      targetFade = 10.0 / sp;  // generic consonant cluster
    }
    // === FALLBACK: sonorant -> unclassified consonant (e.g. /h/) ===
    // /h/ uses aspirationAmplitude not fricationAmplitude, so tokIsFricative
    // returns false.  Without this, transitions like /n/→/h/ get zero fade.
    else if ((prevVowelLike || prevNasal || prevLiquid) &&
             !curVowelLike && !curStop && !curFric && !curNasal && !curLiquid) {
      targetFade = v2fEff;  // treat like vowel->fricative
    }
    else if (!prevVowelLike && !prevStop && !prevFric && !prevNasal && !prevLiquid &&
             (curVowelLike || curNasal || curLiquid)) {
      targetFade = f2v;  // treat like fricative->vowel
    }

    // Apply per-formant transition scaling if the language pack specifies it.
    if (lang.boundarySmoothingF1Scale > 0.0 && lang.boundarySmoothingF1Scale != 1.0) {
      cur.transF1Scale = lang.boundarySmoothingF1Scale;
    }
    if (lang.boundarySmoothingF2Scale > 0.0 && lang.boundarySmoothingF2Scale != 1.0) {
      cur.transF2Scale = lang.boundarySmoothingF2Scale;
    }
    if (lang.boundarySmoothingF3Scale > 0.0 && lang.boundarySmoothingF3Scale != 1.0) {
      cur.transF3Scale = lang.boundarySmoothingF3Scale;
    }

    // Nasal F1 should jump nearly instantly (overrides general F1 scale).
    if (lang.boundarySmoothingNasalF1Instant && (curNasal || prevNasal)) {
      cur.transF1Scale = 0.05;  // 5% of fade = nearly instant
    }

    // Voicing flip (voiced→voiceless or vice-versa): don't increase fade.
    // A longer crossfade across a voicing boundary makes voicing and
    // aspiration overlap, producing a buzz/pop.  Leave the base fade alone.
    // Vowels are inherently voiced, so vowel↔consonant transitions are
    // exempt — those need the longer fade for smooth formant movement.
    const bool prevVoiced = tokIsVowelLike(prev) || tokIsVoiced(prev);
    const bool curVoiced = tokIsVowelLike(cur) || tokIsVoiced(cur);
    const bool voicingFlip = (prevVoiced != curVoiced) &&
                             !prevVowelLike && !curVowelLike;

    // Apply if we have a target and it's larger than current fade
    if (targetFade > 0.0 && targetFade > cur.fadeMs && !voicingFlip) {
      // Cap fade to fraction of duration to preserve phoneme steady-state
      if (cur.durationMs > 0.0) {
        const double maxFade = cur.durationMs * kMaxFadeRatio;
        targetFade = std::min(targetFade, maxFade);
        // Floor: don't let the cap shrink fade below kMinFadeMs.
        // For truly tiny phones, clampFadeToDuration below will
        // ensure fade <= duration.
        if (targetFade < kMinFadeMs) {
          targetFade = std::min(kMinFadeMs, cur.durationMs);
        }
      }

      if (targetFade > cur.fadeMs) {
        cur.fadeMs = targetFade;
        clampFadeToDuration(cur);
      }
    }
  }

  return true;
}

}  // namespace nvsp_frontend::passes