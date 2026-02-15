/*
TGSpeechBox — Boundary smoothing pass (crossfade and formant transitions).
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

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

  // For boundary fade scaling, never let slow speech make fades LONGER
  // than the configured values. Fast speech shortens fades (less time
  // available), but slow speech should NOT stretch them — the ear
  // expects crisper boundaries when phonemes are longer.
  const double fadeSpeed = std::max(sp, 1.0);

  // Per-boundary-type fade times (from lang pack, ms / fadeSpeed).
  const double v2sEff = lang.boundarySmoothingVowelToStopMs / fadeSpeed;
  const double s2vEff = lang.boundarySmoothingStopToVowelMs / fadeSpeed;
  const double v2fEff = lang.boundarySmoothingVowelToFricMs / fadeSpeed;
  const double f2v = lang.boundarySmoothingFricToVowelMs / fadeSpeed;
  const double v2n = lang.boundarySmoothingVowelToNasalMs / fadeSpeed;
  const double n2v = lang.boundarySmoothingNasalToVowelMs / fadeSpeed;
  const double v2l = lang.boundarySmoothingVowelToLiquidMs / fadeSpeed;
  const double l2v = lang.boundarySmoothingLiquidToVowelMs / fadeSpeed;
  const double n2s = lang.boundarySmoothingNasalToStopMs / fadeSpeed;
  const double l2s = lang.boundarySmoothingLiquidToStopMs / fadeSpeed;
  const double f2s = lang.boundarySmoothingFricToStopMs / fadeSpeed;
  const double s2f = lang.boundarySmoothingStopToFricMs / fadeSpeed;
  const double v2v = lang.boundarySmoothingVowelToVowelMs / fadeSpeed;

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

    // Aspiration-aware bypass: don't stretch the fade for tokens whose
    // onset is aspiration-dominant (e.g. /h/, voiceless fricatives).
    // Aspiration needs a crisp onset — a gradual fade-in sounds mushy.
    const bool curAspirationDominant =
        cur.field[static_cast<int>(FieldId::aspirationAmplitude)] > 0.08 &&
        cur.field[static_cast<int>(FieldId::voiceAmplitude)] < 0.1;

    // Voicing flip guard: don't stretch fade across voiced↔voiceless
    // consonant boundaries (buzz/pop from overlapping voicing+aspiration).
    // Exempt: vowels (inherently voiced), stops (closure cuts voicing),
    // and word boundaries (gap serves as natural cutoff).
    const bool prevVoiced = tokIsVowelLike(prev) || tokIsVoiced(prev);
    const bool curVoiced = tokIsVowelLike(cur) || tokIsVoiced(cur);
    const bool voicingFlip = (prevVoiced != curVoiced) &&
                             !prevVowelLike && !curVowelLike &&
                             !prevStop && !curStop &&
                             !cur.wordStart;

    // Stretch the amplitude fade at this boundary.
    if (targetFade > 0.0 && targetFade > cur.fadeMs &&
        !voicingFlip && !curAspirationDominant) {
      // Cap fade to fraction of duration to preserve steady-state.
      if (cur.durationMs > 0.0) {
        const double maxFade = cur.durationMs * kMaxFadeRatio;
        targetFade = std::min(targetFade, maxFade);
        if (targetFade < kMinFadeMs) {
          targetFade = std::min(kMinFadeMs, cur.durationMs);
        }
      }
      if (targetFade > cur.fadeMs) {
        cur.fadeMs = targetFade;
        clampFadeToDuration(cur);
      }
    }

    // ── Place-aware formant transition speeds ──
    //
    // Formants lead, amplitude follows.  transF*Scale < 1.0 makes
    // formant frequencies reach their target in the first 30-70% of
    // the fade, then hold steady while amplitude finishes naturally.
    //
    // Different articulators move at different speeds:
    //   Lips (labial): light, fast — F2/F3 drift (independent of tongue)
    //   Tongue tip (alveolar): light, precise — F2/F3 fast
    //   Tongue blade (palatal): medium — F3 slow (F3 IS the place cue)
    //   Tongue body (velar): heavy — F2 slow (F2 IS the place cue)

    // Check if the next real token (past micro-gaps) is silence or end.
    bool nextIsSilence = true;
    for (size_t j = static_cast<size_t>(i) + 1; j < tokens.size(); ++j) {
      const Token& nxt = tokens[j];
      if (!tokIsSilenceOrMissing(nxt)) {
        nextIsSilence = false;
        break;
      }
      if (nxt.silence && !nxt.preStopGap && !nxt.clusterGap &&
          !nxt.vowelHiatusGap) {
        break;  // real silence boundary
      }
    }

    // Pre-silence protection: if a vowel is the last sound before silence,
    // do NOT apply transScales.  The vowel should hold steady and just fade
    // in amplitude.  Formant bending into silence sounds metallic.
    if (curVowelLike && nextIsSilence) {
      // Leave transF*Scale at 0.0 (= no override, use full fade duration).
      // The amplitude fade handles the offset into silence.
      continue;
    }

    // V→V (hiatus, not tied diphthongs): both sides are targets, no rushing.
    if (prevVowelLike && curVowelLike) {
      continue;
    }

    // Identify the consonant in this transition for place lookup.
    const Token* consToken = nullptr;
    if (curVowelLike && !prevVowelLike && !tokIsSilenceOrMissing(prev)) {
      consToken = &prev;       // C → V transition
    } else if (prevVowelLike && !curVowelLike) {
      consToken = &cur;        // V → C transition
    } else if (!prevVowelLike && !curVowelLike &&
               !tokIsSilenceOrMissing(prev)) {
      consToken = &cur;        // C → C: use incoming consonant
    }

    Place place = Place::Unknown;
    if (consToken && consToken->def) {
      place = getPlace(consToken->def->key);
    }

    double f1s, f2s, f3s;
    switch (place) {
      case Place::Labial:
        f1s = lang.boundarySmoothingLabialF1Scale;
        f2s = lang.boundarySmoothingLabialF2Scale;
        f3s = lang.boundarySmoothingLabialF3Scale;
        break;
      case Place::Alveolar:
        f1s = lang.boundarySmoothingAlveolarF1Scale;
        f2s = lang.boundarySmoothingAlveolarF2Scale;
        f3s = lang.boundarySmoothingAlveolarF3Scale;
        break;
      case Place::Palatal:
        f1s = lang.boundarySmoothingPalatalF1Scale;
        f2s = lang.boundarySmoothingPalatalF2Scale;
        f3s = lang.boundarySmoothingPalatalF3Scale;
        break;
      case Place::Velar:
        f1s = lang.boundarySmoothingVelarF1Scale;
        f2s = lang.boundarySmoothingVelarF2Scale;
        f3s = lang.boundarySmoothingVelarF3Scale;
        break;
      default:
        // Unknown place: fall back to global defaults.
        f1s = lang.boundarySmoothingF1Scale;
        f2s = lang.boundarySmoothingF2Scale;
        f3s = lang.boundarySmoothingF3Scale;
        break;
    }

    // V→C direction adjustment: slow the departure so the vowel holds
    // its identity longer before the consonant's influence takes over.
    if (prevVowelLike && !curVowelLike) {
      constexpr double departSlowdown = 1.4;  // 40% slower departure
      f1s = std::min(f1s * departSlowdown, 1.0);
      f2s = std::min(f2s * departSlowdown, 1.0);
      f3s = std::min(f3s * departSlowdown, 1.0);
    }

    // Utterance-final consonant: use gentler scales — there's no
    // following target to rush toward.
    if (!curVowelLike && nextIsSilence) {
      f1s = std::min(f1s * 1.5, 1.0);
      f2s = std::min(f2s * 1.5, 1.0);
      f3s = std::min(f3s * 1.5, 1.0);
    }

    if (f1s > 0.0) cur.transF1Scale = f1s;
    if (f2s > 0.0) cur.transF2Scale = f2s;
    if (f3s > 0.0) cur.transF3Scale = f3s;

    // Nasal F1 should jump nearly instantly (overrides the above).
    if (lang.boundarySmoothingNasalF1Instant && (curNasal || prevNasal)) {
      cur.transF1Scale = 0.05;  // 5% of fade = nearly instant
    }
  }

  return true;
}

}  // namespace nvsp_frontend::passes