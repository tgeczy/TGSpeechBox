/*
TGSpeechBox — Amplitude contour pass (loudness level, onset glide, fall).
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.

Why this pass exists
--------------------
Every voiced segment we emit used to be one amplitude held flat for its
whole duration, joined to its neighbours by a 3-10 ms crossfade, and the
packs give almost every voiced phoneme the same source amplitude (0.9).
Measured on a paragraph: our nasals sit 3 dB under the loudest vowel, our
voiced fricatives 2 dB under, unstressed vowels 2.6 dB under.  The
ETI-Eloquence-lineage reference renders we measure against look nothing
like that on an envelope plot: every boundary is a slope of 20-60 ms,
never a step; a stressed vowel rises over ~20 ms, holds near its peak and
decays a little; an unstressed vowel sits 3-4 dB lower and decays more; a
nasal starts near the vowel's level and decays ~8 dB toward the closure; a
voiced fricative is a V-shaped valley 15-25 dB deep; a closure is 20 dB
down.  Measured as 20 ms frame levels, our 10th-90th percentile span was
~15 dB with 68% of voiced frames within 6 dB of the peak; the reference
spans ~26 dB with 16%.  Half the dynamic range, at the syllable rate, is a
large part of what the ear reports as "spliced" or "flat" — and levels
alone, stepped at the crossfade, read as a compressor pumping (ear-tested
2026-09-14).  The contour needs all three pieces:

  level  : outputGain *= 10^(levelDb/20)
           A per-class / per-stress offset in dB relative to a primary-
           stressed vowel, on the frame's master gain, so the DSP's
           source-keyed behaviour (voiced-fricative noise duck, bypass
           duck, equal-power crossfade decision) is untouched.
  onset  : amplitudeOnsetMs
           The DSP glides voicing amplitude and master gain in from
           whatever was playing at the boundary over this time (capped at
           40% of the segment), so a vowel after a nasal rises instead of
           jumping and a fricative eases down into its valley.
  fall   : endVoiceAmplitudeScale = 10^(-fallDb * durationScale / 20)
           The DSP ramps voiceAmplitude to voiceAmplitude*scale over the
           rest of the segment.  Sonorants only (their single source is
           voicing).  durationScale shrinks the fall for segments shorter
           than amplitudeContourMinMs, so a 20 ms vowel at 3x is not wiped.

Class levels follow Klatt (1980, J. Acoust. Soc. Am. 67:971) and Klatt &
Klatt (1990); the slopes are Klatt's piecewise-linear source interpolation;
the stressed/unstressed shapes and the nasal decay are measured against the
reference renders.  All knobs are per language, default off; see Tuning.md.

Runs last in PostTiming so it sees final tokens and final durations, on top
of whatever prominence realised.  Gaps, closures, taps and trills (which
have their own micro-events) and stops/affricates (whose voice bar and
burst have their own amplitudes) are left alone.
*/

#include "amplitude_contour.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nvsp_frontend::passes {

namespace {

static inline bool isGapLike(const Token& t) {
  return t.silence || !t.def || t.preStopGap || t.clusterGap ||
         t.nasalToStopGap || t.vowelHiatusGap ||
         t.voicedClosure || t.codaFricStopBlend;
}

static inline double currentField(const Token& t, int idx, double fallback) {
  const std::uint64_t bit = 1ULL << idx;
  if (t.setMask & bit) return t.field[idx];
  if (t.def && (t.def->setMask & bit)) return t.def->field[idx];
  return fallback;
}

static inline double dbToLinear(double db) {
  return std::pow(10.0, db / 20.0);
}

}  // namespace

bool runAmplitudeContour(PassContext& ctx, std::vector<Token>& tokens, std::string& outError) {
  (void)outError;
  const auto& lang = ctx.pack.lang;
  if (!lang.amplitudeContourEnabled) return true;

  const int vaIdx = static_cast<int>(FieldId::voiceAmplitude);
  const int faIdx = static_cast<int>(FieldId::fricationAmplitude);
  const int ogIdx = static_cast<int>(FieldId::outputGain);
  const double minMs = std::max(1.0, lang.amplitudeContourMinMs);

  // Clause shape: where each token sits in time (for the declination line)
  // and where the last word begins (for the final lowering).
  std::vector<double> startMs(tokens.size(), 0.0);
  double totalMs = 0.0;
  size_t lastWord = 0;
  for (size_t i = 0; i < tokens.size(); ++i) {
    startMs[i] = totalMs;
    totalMs += std::max(0.0, tokens[i].durationMs);
    if (!isGapLike(tokens[i]) && tokens[i].wordStart) lastWord = i;
  }

  for (size_t i = 0; i < tokens.size(); ++i) {
    Token& t = tokens[i];
    if (isGapLike(t)) continue;
    const std::uint32_t f = t.def->flags;
    if ((f & (kIsTap | kIsTrill)) != 0) continue;

    double levelDb = 0.0;
    double fallDb = 0.0;
    bool sonorant = true;
    bool obstruent = false;  // level only: no onset glide, no fall
    if ((f & kIsStop) != 0 || t.postStopAspiration) {
      levelDb = lang.amplitudeContourStopLevelDb;
      obstruent = true;
      sonorant = false;
    } else if ((f & kIsVowel) != 0) {
      if (t.stress == 1) {
        fallDb = lang.amplitudeContourStressedFallDb;
      } else if (t.stress == 2) {
        levelDb = 0.5 * lang.amplitudeContourUnstressedLevelDb;
        fallDb = 0.5 * (lang.amplitudeContourStressedFallDb + lang.amplitudeContourUnstressedFallDb);
      } else {
        levelDb = lang.amplitudeContourUnstressedLevelDb;
        fallDb = lang.amplitudeContourUnstressedFallDb;
      }
    } else if ((f & kIsNasal) != 0) {
      levelDb = lang.amplitudeContourNasalLevelDb;
      fallDb = lang.amplitudeContourNasalFallDb;
    } else if ((f & (kIsLiquid | kIsSemivowel)) != 0) {
      levelDb = lang.amplitudeContourGlideLevelDb;
      fallDb = lang.amplitudeContourSonorantFallDb;
    } else if ((f & kIsAfricate) != 0) {
      levelDb = ((f & kIsVoiced) != 0) ? lang.amplitudeContourVoicedAffricateLevelDb
                                       : lang.amplitudeContourVoicelessFricLevelDb;
      obstruent = true;
      sonorant = false;
    } else if ((f & kIsVoiced) != 0 && currentField(t, faIdx, 0.0) > 0.05) {
      levelDb = lang.amplitudeContourVoicedFricLevelDb;  // voiced fricative
      sonorant = false;
    } else if ((f & kIsVoiced) == 0) {
      levelDb = lang.amplitudeContourVoicelessFricLevelDb;  // s ʃ f θ h ...
      obstruent = true;
      sonorant = false;
    } else {
      continue;
    }
    levelDb += lang.amplitudeContourMakeupDb;
    // Declination: the whole clause slopes down, like F0 does.
    if (totalMs > 0.0) levelDb -= lang.amplitudeContourDeclinationDb * (startMs[i] / totalMs);
    // Final word: lower still, and its stressed vowel falls away.
    if (i >= lastWord) {
      levelDb += lang.amplitudeContourFinalLevelDb;
      if ((f & kIsVowel) != 0 && t.stress == 1) fallDb = std::max(fallDb, lang.amplitudeContourFinalFallDb);
    }

    if (levelDb != 0.0) {
      const double og = currentField(t, ogIdx, lang.defaultOutputGain);
      if (og > 0.0) {
        t.field[ogIdx] = og * dbToLinear(levelDb);
        t.setMask |= (1ULL << ogIdx);
      }
    }
    if (lang.amplitudeContourOnsetMs > 0.0 && !obstruent) {
      t.amplitudeOnsetMs = std::min(lang.amplitudeContourOnsetMs, 0.4 * t.durationMs);
    }
    if (sonorant && fallDb > 0.0 && currentField(t, vaIdx, 0.0) > 0.0) {
      // Short segments (fast rates) get a proportionally smaller fall.
      const double durScale = std::clamp(t.durationMs / minMs, 0.0, 1.0);
      t.endVoiceAmplitudeScale = dbToLinear(-fallDb * durScale);
    }
  }
  return true;
}

}  // namespace nvsp_frontend::passes
