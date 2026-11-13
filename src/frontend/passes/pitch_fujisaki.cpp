/*
TGSpeechBox — Fujisaki pitch model pass.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

// =============================================================================
// Fujisaki Pitch Pass — pitch contour generation
// =============================================================================
//
// Replaces the old calculatePitchesFujisaki() from ipa_engine.cpp.
//
// Architecture: the frontend computes a smoothly declining base pitch using
// exponential decay (no hard floor, no kink).  The DSP's Fujisaki phrase
// and accent filters add local peaks on top.
//
// The original code used linear declination + a hard declinMax floor, which
// created an audible "kink" where the slope suddenly changed.  Exponential
// decay naturally asymptotes — fast initial fall, gradually flattening — so
// long sentences decline smoothly without ever hitting a wall.
//
// Multi-phrase (firing at every word boundary) was tried and reverted: the
// DSP phrase filter peaks at ~193ms and decays quickly, so overlapping humps
// at word boundaries created a "mechanical bull" effect rather than smooth
// declination.  The phrase filter is designed for local emphasis, not for
// creating the overall falling baseline.

#include "pitch_fujisaki.h"
#include "pitch_common.h"
#include "../ipa_engine.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace nvsp_frontend {

namespace {

// The accent filter of src/pitchModel.h is critically damped: x rise times
// into a command it has reached this share of the command's amplitude.
double accentStep(double x) {
  return (x > 0.0) ? 1.0 - (1.0 + x) * std::exp(-x) : 0.0;
}

// The share a command x rise times long reaches at its highest point, which
// comes shortly after the command ends.
double accentCommandPeak(double x) {
  if (!(x > 0.0)) return 0.0;
  const double u = x / (1.0 - std::exp(-x));  // when the response turns
  return accentStep(u) - accentStep(u - x);
}

} // namespace

void applyPitchFujisaki(
    std::vector<Token>& tokens,
    const PackSet& pack,
    double speed,
    double basePitch,
    double inflection,
    char clauseType) {

  if (tokens.empty()) return;

  const auto& lang = pack.lang;

  // Scale phrase/accent amplitudes by inflection (0..1)
  // At inflection=0, prosody is completely flat
  // At inflection=1, full Fujisaki contour
  const double phraseAmp = lang.fujisakiPhraseAmp * inflection;
  const double primaryAccentAmp = lang.fujisakiPrimaryAccentAmp * inflection;
  const double secondaryAccentAmp = lang.fujisakiSecondaryAccentAmp * inflection;

  // Prominence-to-pitch curve shaping
  const double pitchExp = std::max(0.1, lang.fujisakiPitchCurveExponent);

  // Monosyllable accent duration scaling
  const double monoAccDurScale = lang.fujisakiMonoAccentDurScale;

  // Compound declination step (log-F0 units)
  const double compoundStep = lang.fujisakiCompoundDeclinStep;

  // Accent mode: "all", "first_only", or "off"
  const std::string& accentMode = lang.fujisakiAccentMode;
  const bool accentsEnabled = (accentMode != "off");
  const bool firstOnly = (accentMode == "first_only");

  // Clause-type modifiers — driven by per-language-pack overrides.
  // Each clause type has 6 knobs exposed via YAML (flat or nested).
  // Statement ('.') uses identity defaults (all 1.0 / 0.0).
  const LanguagePack::FujisakiClauseOverrides* co = nullptr;
  if (clauseType == '?')      co = &lang.fujisakiQuestion;
  else if (clauseType == '!') co = &lang.fujisakiExclamation;
  else if (clauseType == ',') co = &lang.fujisakiComma;

  double effectivePhraseAmp = phraseAmp;
  double accentBoost = 1.0;
  double finalRiseAmp = 0.0;
  double finalDropScale = 0.0;
  double declinationMul = 1.0;
  const double callerPitch = basePitch;  // before the clause's own scale

  if (co) {
    effectivePhraseAmp *= co->phraseAmpScale;
    accentBoost = co->accentBoost;
    declinationMul = co->declinationScale;
    basePitch *= co->basePitchScale;
    if (co->finalRiseScale > 0.0)
      finalRiseAmp = primaryAccentAmp * co->finalRiseScale;
    finalDropScale = co->finalDropScale;
  }

  // The statement, the exclamation and the question size their commands to
  // the clause and the syllable, and are kept inside the range of
  // pitch_common.h (#155).  Any other clause (the comma) is left exactly as
  // it was: a reporter of #155 called the Fujisaki comma right.
  const bool shapedClause =
      (clauseType == '.' || clauseType == '!' || clauseType == '?');

  // The commands multiply the pitch by exp(amplitude).  When the phrase
  // command and the strongest accent together could take a peak past the
  // ceiling (an exclamation at full inflection reached 3.1 times the
  // caller's pitch), all of them are scaled down together.  With the
  // default amplitudes only the exclamation gets there, from an inflection
  // of 0.54 up.
  if (shapedClause && callerPitch > 0.0 && basePitch > 0.0) {
    const double room = std::log(kPitchCeilRatio * callerPitch / basePitch);
    const double asked = effectivePhraseAmp +
        std::max(primaryAccentAmp, secondaryAccentAmp) * accentBoost;
    if (asked > room && asked > 0.0) {
      const double k = std::max(0.0, room) / asked;
      effectivePhraseAmp *= k;
      accentBoost *= k;
    }
  }

  // -------------------------------------------------------------------------
  // Exponential declination
  // -------------------------------------------------------------------------
  // Formula: curPitch = basePitch * exp(-rate * timeMs)
  //
  // rate is derived from inflection and a tuning scale.  Higher inflection
  // means faster pitch fall.  The exponential naturally asymptotes, so there
  // is no hard floor and no "kink" where the slope suddenly changes.
  //
  // At rate=0.0004 and 2000ms: pitch = base * exp(-0.8) ≈ 45% of base range
  // At rate=0.0002 and 2000ms: pitch = base * exp(-0.4) ≈ 67% of base range
  //
  // The inflection slider (0-1) scales this:
  //   inflection=0   → rate=0 (flat)
  //   inflection=0.5 → moderate decline
  //   inflection=1   → full decline
  // -------------------------------------------------------------------------
  double inflScale = lang.legacyPitchInflectionScale;
  // 0 means no declination; it used to be read as 1, so the slider's lowest
  // position gave more of it than its middle.  (At 0 the whole clause is
  // flat, as in every other mode: ipa_engine.cpp passes no inflection.)
  if (!(inflScale >= 0.0)) inflScale = 1.0;  // negative or not a number: as before
  if (inflScale > 2.0) inflScale = 2.0;

  // fujisakiDeclinationRate: new setting, controls the exponential decay
  // steepness.  Default 0.0003 gives natural-sounding decline for typical
  // sentences.  Lower = gentler, higher = steeper.
  double declinRate = lang.fujisakiDeclinationRate;
  if (declinRate <= 0.0) declinRate = 0.0003;  // sane default

  // Length-aware adjustment: slow down declination for longer phrases so
  // the final word still has pitch headroom.  For a reference-length phrase
  // (~1500ms of phonetic content) the configured rate is used as-is.
  // Longer phrases scale the rate down proportionally.
  const double referenceDurationMs = 1500.0;
  double totalPhraseDurationMs = 0.0;
  for (const auto& tok : tokens) {
    if (!tok.silence && tok.def) totalPhraseDurationMs += tok.durationMs;
  }
  const double lengthFactor = referenceDurationMs /
      std::max(referenceDurationMs, totalPhraseDurationMs);

  // Final rate incorporating inflection, scale, clause type, speed, and
  // phrase length.  speed > 1 = faster speech = compress the declination
  // into less time.
  const double effectiveRate = declinRate * inflection * inflScale
      * declinationMul * speed * lengthFactor;

  const int vp = static_cast<int>(FieldId::voicePitch);
  const int evp = static_cast<int>(FieldId::endVoicePitch);

  // First pass: find the last vowel nucleus for clause-final pitch shaping.
  int lastVowelIdx = -1;
  for (int i = static_cast<int>(tokens.size()) - 1; i >= 0; --i) {
    const Token& t = tokens[static_cast<size_t>(i)];
    if (t.silence || !t.def) continue;
    if (tokenIsVowel(t)) {
      lastVowelIdx = i;
      break;
    }
  }
  // Fallback: if we somehow have no vowel, use the last non-silence token.
  if (lastVowelIdx < 0) {
    for (int i = static_cast<int>(tokens.size()) - 1; i >= 0; --i) {
      const Token& t = tokens[static_cast<size_t>(i)];
      if (!t.silence && t.def) {
        lastVowelIdx = i;
        break;
      }
    }
  }

  // Pre-pass: count vowel nuclei per word for monosyllable detection and
  // compound boundary tracking.  wordNuclei[i] = nucleus count of the word
  // containing token i.  wordHadPrimary[i] = true if a primary-stressed
  // vowel appeared earlier in the same word.
  std::vector<int> wordNuclei(tokens.size(), 0);
  std::vector<bool> wordHadPrimary(tokens.size(), false);
  {
    size_t wordBegin = 0;
    for (size_t i = 0; i <= tokens.size(); ++i) {
      bool isEnd = (i == tokens.size()) ||
                   (i > 0 && tokens[i].wordStart && !tokens[i].silence);
      if (isEnd) {
        // Count nuclei in [wordBegin, i)
        int nuclei = 0;
        bool seenPrimary = false;
        for (size_t j = wordBegin; j < i; ++j) {
          if (!tokens[j].silence && tokens[j].def && tokenIsVowel(tokens[j])) {
            ++nuclei;
          }
        }
        // Second pass: mark per-token nucleus count and primary tracking
        bool primarySoFar = false;
        for (size_t j = wordBegin; j < i; ++j) {
          wordNuclei[j] = nuclei;
          wordHadPrimary[j] = primarySoFar;
          if (!tokens[j].silence && tokens[j].def && tokenIsVowel(tokens[j])
              && tokens[j].prominence >= 0.9) {
            primarySoFar = true;
          }
        }
        if (i < tokens.size()) wordBegin = i;
      }
    }
  }

  // The command lengths in a frame are in samples at 22050 Hz.
  const double kSamplesPerMs = 22.05;
  const double kDspPhraseLen = 4250.0;  // the DSP's defaults (src/pitchModel.h)
  const double kDspAccentLen = 1024.0;
  const double kPhraseLenClauseFraction = 0.25;
  double finalVowelEndPitch = 0.0;  // where a shaped last vowel ended

  // An accent command sized to its syllable, unless the pack fixes a length.
  //
  // In Fujisaki's model the accent command is a rectangular pulse over the
  // accented mora or syllable, and the F0 response to it has a time
  // constant of about 50 ms.  In Japanese the command starts and ends a few
  // tens of ms before the segments it belongs to (Fujisaki & Hirose 1984,
  // "Analysis of voice fundamental frequency contours for declarative
  // sentences of Japanese", J. Acoust. Soc. Jpn. (E) 5(4), 233-242;
  // Fujisaki, Ohno & Ueno 1996, "Timing of fundamental frequency control in
  // word accent of the common Japanese", J. Acoust. Soc. Am. 99(4),
  // doi:10.1121/1.415625).  In German read at five tempos a rising accent's
  // command starts 104 ms after the syllable does and 12 ms after its
  // vowel, on average, and nearer the syllable's start the faster the
  // speech (Mixdorff, Leemann & Dellwo 2014, "The influence of speech rate
  // on Fujisaki model parameters", EURASIP J. Audio, Speech, and Music
  // Processing, doi:10.1186/s13636-014-0033-6).  Here it starts with the
  // syllable.  The DSP's command was 340 ms at every rate, starting with
  // the vowel, so at rate 3 one accent covered the rest of the clause and
  // held its end up (#155).
  //
  // At fast rates a syllable is shorter than the filter's rise time (46 ms)
  // and the accent would reach a fraction of its height.  Speakers do not
  // lose it that way: in the same study the accent command's amplitude
  // stayed at 0.33 to 0.35 from very slow to fast speech and fell to 0.26
  // only at the fastest rate people could manage, 1.6 times normal.
  // Nothing is measured at the two and three times a screen reader runs
  // at.  So, as a choice: above rate 1 the filter's rise time shrinks with
  // the rate, as the syllables do, and each command's amplitude is raised
  // by what the filter leaves short of it, so that the peak is the height
  // the pack asks for at every rate.
  //
  // The rise time is one for the whole clause.  The DSP has a single accent
  // filter, which every command designs anew: a longer rise time given
  // while the accent before is still dying away carries that accent's fall
  // on at the slower pace, and the pitch dips under the line.
  const bool sizedAccents = shapedClause && lang.fujisakiAccentDur <= 0.0;
  const double packAccentLen =
      (lang.fujisakiAccentLen > 0.0) ? lang.fujisakiAccentLen : kDspAccentLen;
  const double accentLen = std::max(1.0, packAccentLen / std::max(1.0, speed));
  // The last sized command, for what is left of it where the clause ends.
  double lastCmdStartMs = 0.0, lastCmdSamples = 0.0, lastCmdAmp = 0.0;
  auto sizeAccent = [&](Token& carrier, double atMs, double ms, double amp) {
    const double samples = std::max(1.0, ms * kSamplesPerMs);
    // (Not under a quarter of a rise time, where the share goes to nothing:
    // a guard for a syllable of a few ms, not a tuned value.)
    carrier.fujisakiAccentAmp =
        amp / accentCommandPeak(std::max(0.25, samples / accentLen));
    carrier.fujisakiAccentDur = samples;
    carrier.fujisakiAccentLen = accentLen;
    carrier.fujisakiAccentDurScale = 1.0;
    lastCmdStartMs = atMs;
    lastCmdSamples = samples;
    lastCmdAmp = carrier.fujisakiAccentAmp;
  };
  // When each token starts, closures and gaps included: the DSP's clock
  // runs through them.
  std::vector<double> startMs(tokens.size() + 1, 0.0);
  for (size_t i = 0; i < tokens.size(); ++i) {
    startMs[i + 1] = startMs[i] + tokens[i].durationMs;
  }

  bool isFirstFrame = true;
  bool hadFirstAccent = false;  // Track if we've placed the first accent
  int pendingStress = 0;        // Stress carried from syllableStart to vowel nucleus
  double durationCounter = 0.0; // Accumulated time for declination

  for (size_t i = 0; i < tokens.size(); ++i) {
    Token& t = tokens[i];
    if (t.silence || !t.def) continue;

    // Exponential declination: smooth, no kink, naturally asymptotes.
    double curBasePitch = basePitch * exp(-effectiveRate * durationCounter);
    double endBasePitch = basePitch * exp(-effectiveRate * (durationCounter + t.durationMs));
    durationCounter += t.durationMs;

    // Set declining base pitch (DSP Fujisaki accents add peaks on top)
    t.field[vp] = curBasePitch;
    t.field[evp] = endBasePitch;
    // A consonant after the last vowel stays where the vowel ended.  It used
    // to go back to the declination line: after a question's rise that was
    // 30 Hz down again, and up again after a statement's fall (#155).
    if (finalVowelEndPitch > 0.0) {
      t.field[vp] = finalVowelEndPitch;
      t.field[evp] = finalVowelEndPitch;
    }
    t.setMask |= (1ull << vp) | (1ull << evp);

    // Enable Fujisaki on all phonetic tokens.
    // Even during unvoiced segments, we still want time to advance so the
    // contour is ready when voicing resumes.
    t.fujisakiEnabled = true;
    // On a word-initial last vowel the word-boundary dip frame would run
    // the whole final movement in 8 ms and the vowel would start it again.
    t.pitchRampAcrossWordDip = shapedClause;

    // Single phrase command at utterance start.
    // The DSP phrase filter (pitchModel.h) creates a local hump from this
    // impulse — it peaks at ~193ms and decays naturally.  The overall
    // declining baseline comes from the exponential formula above, not
    // from the phrase filter's decay.
    if (isFirstFrame) {
      t.fujisakiReset = true;
      t.fujisakiPhraseAmp = effectivePhraseAmp;
      // In Fujisaki's model the phrase command comes before the utterance
      // (about 475 ms before its first segment in Mixdorff, Leemann & Dellwo
      // 2014, "The influence of speech rate on Fujisaki model parameters",
      // EURASIP J. Audio, Speech, and Music Processing,
      // doi:10.1186/s13636-014-0033-6), so its component is past its peak
      // and falling through the phrase.  Ours fires with the first frame and
      // peaks one rise time later (193 ms by default): a one-word clause
      // ended on that peak, 12% above the pitch it was given, and could not
      // fall (#155).  The rise time is at most a quarter of the clause,
      // which leaves a fifth of the peak at the clause's end.  (A quarter
      // is a choice, not a measurement.)
      if (shapedClause) {
        const double clauseSamples = totalPhraseDurationMs * kSamplesPerMs;
        const double phraseLen =
            (lang.fujisakiPhraseLen > 0.0) ? lang.fujisakiPhraseLen : kDspPhraseLen;
        t.fujisakiPhraseLen = std::max(1.0,
            std::min(phraseLen, clauseSamples * kPhraseLenClauseFraction));
      }
      isFirstFrame = false;
    }

    // Track syllable stress at the syllable boundary...
    if (t.syllableStart) {
      pendingStress = t.stress;
    }

    // ...but place the accent command on the vowel nucleus.
    if (accentsEnabled && tokenIsVowel(t)) {
      bool shouldAccent = false;
      double accentAmp = 0.0;

      // When prominence is available and pitchFromProminence is enabled,
      // use the prominence score to drive accent amplitude.
      if (lang.prominencePitchFromProminence && t.prominence >= 0.0) {
        // Any vowel with prominence > small threshold gets an accent.
        if (t.prominence > 0.05) {
          if (firstOnly) {
            if (!hadFirstAccent) {
              shouldAccent = true;
              hadFirstAccent = true;
            }
          } else {
            shouldAccent = true;
          }
          // Improvement 1: Power curve on prominence for better contrast.
          // pow(0.6, 1.4) ≈ 0.49 vs linear 0.60 — audible separation.
          double shaped = pow(t.prominence, pitchExp);
          accentAmp = primaryAccentAmp * accentBoost * shaped;

          // Improvement 2: Monosyllable accent timing.
          // Single-nucleus content words get a shorter, punchier accent.
          if (monoAccDurScale < 1.0 && wordNuclei[i] == 1) {
            t.fujisakiAccentDurScale = monoAccDurScale;
          }

          // Improvement 3: Compound declination step.
          // Secondary-stressed vowel after primary in the same word →
          // small pitch step-down to reinforce primary-secondary contrast.
          if (compoundStep > 0.0 && wordHadPrimary[i] && t.prominence < 0.9) {
            accentAmp = std::max(0.0, accentAmp - compoundStep);
          }
        }
        pendingStress = 0;
      } else if (pendingStress != 0) {
        // Original stress-mark-based accent logic (unchanged).
        if (pendingStress == 1) {
          if (firstOnly) {
            if (!hadFirstAccent) {
              shouldAccent = true;
              hadFirstAccent = true;
            }
          } else {
            shouldAccent = true;
          }
          accentAmp = primaryAccentAmp * accentBoost;
        } else if (pendingStress == 2 && !firstOnly) {
          shouldAccent = true;
          accentAmp = secondaryAccentAmp * accentBoost;
        }
        pendingStress = 0;
      }

      if (shouldAccent) {
        t.fujisakiAccentAmp = accentAmp;
        // The command spans the syllable: from the consonant the syllable
        // starts with, so it leads the vowel, to where the next syllable
        // starts.  (The clause's last vowel is handled below.)
        if (sizedAccents && static_cast<int>(i) != lastVowelIdx) {
          size_t first = i;
          if (!t.syllableStart) {
            for (size_t j = i; j-- > 0; ) {
              const Token& p = tokens[j];
              if (!p.silence && p.def && tokenIsVowel(p)) break;  // the syllable before
              if (p.syllableStart) { first = j; break; }
            }
          }
          // The command goes out with its token's first frame, which a
          // closure or a gap does not have, and the DSP fires it on a rising
          // edge, so the sound before it must carry none.  Otherwise it
          // starts with the vowel, as it always did.
          while (first < i && (tokens[first].silence || !tokens[first].def ||
                               tokens[first].voicedClosure)) {
            ++first;
          }
          if (first < i) {
            for (size_t j = first; j-- > 0; ) {
              const Token& p = tokens[j];
              if (p.silence || !p.def) continue;
              if (p.fujisakiAccentAmp > 0.0) first = i;
              break;
            }
          }
          size_t next = i + 1;
          while (next < tokens.size() &&
                 !tokens[next].syllableStart && !tokens[next].wordStart) {
            ++next;
          }
          double ms = startMs[next] - startMs[first];
          ms *= t.fujisakiAccentDurScale;  // a monosyllable's shorter accent
          t.fujisakiAccentAmp = 0.0;
          t.fujisakiAccentDurScale = 1.0;
          sizeAccent(tokens[first], startMs[first], ms, accentAmp);
        }
      }
    }

    // Final vowel: direct pitch shaping for clause-type identity.
    // This is essential for short utterances where exponential declination
    // barely has time to create within-word pitch movement.
    if (static_cast<int>(i) == lastVowelIdx) {
      if (clauseType == '?') {
        // Question: pitch RISES across final vowel.
        // The accent command adds a hump on top, but the base contour
        // itself must end higher to sound like a question.
        t.field[evp] = t.field[vp] * 1.25;  // end 25% higher
        if (finalRiseAmp > 0.0) {
          t.fujisakiAccentAmp = std::max(t.fujisakiAccentAmp, finalRiseAmp);
        }
        finalVowelEndPitch = t.field[evp];
      } else if (clauseType == '!' && finalDropScale > 0.0) {
        // Exclamation: snap DOWN (existing behavior, preserved exactly)
        t.fujisakiAccentAmp = 0.0;
        double dropFactor = 1.0 + finalDropScale;
        t.field[vp] /= dropFactor;
        t.field[evp] /= (dropFactor * 1.3);
        finalVowelEndPitch = t.field[evp];
      } else if (clauseType == '.') {
        // Statement: ensure pitch FALLS on final vowel.
        // For long sentences the declination already does this,
        // but for single words we need a direct nudge.
        t.field[evp] *= 0.85;  // end 15% lower
        // No accent command on the clause's last vowel, as in the
        // exclamation: a command that starts here is at its highest where
        // the clause ends, and took the fall back.  The fall is the accent.
        t.fujisakiAccentAmp = 0.0;
        finalVowelEndPitch = t.field[evp];
      }
      // Comma: no final shaping (continuation = level/slight rise)

      // A command on the last vowel (the question's rise) runs to the end of
      // the clause, through the same filter: it is a rise, not a peak, and
      // gets as far by the end of the clause as the filter lets it (above
      // rate 1, as far as at rate 1).  And it takes the clause's end no
      // further than the ceiling, counting what the accent before it still
      // adds there.
      if (sizedAccents && t.fujisakiAccentAmp > 0.0) {
        double restMs = 0.0;
        for (size_t j = i; j < tokens.size(); ++j) {
          if (!tokens[j].silence && tokens[j].def) restMs += tokens[j].durationMs;
        }
        const double samples = std::max(1.0, restMs * kSamplesPerMs);
        const double reach = accentStep(samples / accentLen);
        const double sinceLast =
            (startMs[i] + restMs - lastCmdStartMs) * kSamplesPerMs / accentLen;
        const double left = lastCmdAmp *
            (accentStep(sinceLast) - accentStep(sinceLast - lastCmdSamples / accentLen));
        double wanted = t.fujisakiAccentAmp * reach;
        if (t.field[evp] > 0.0 && callerPitch > 0.0) {
          wanted = std::min(wanted, std::max(0.0,
              std::log(kPitchCeilRatio * callerPitch / t.field[evp]) - left));
        }
        t.fujisakiAccentAmp = (reach > 0.0) ? wanted / reach : 0.0;
        t.fujisakiAccentDur = samples;
        t.fujisakiAccentLen = accentLen;
        t.fujisakiAccentDurScale = 1.0;
      }
    }
  }

  // The frames' own pitch (the declination and the last vowel's movement)
  // stays in range too: at the top of the inflection scale an exclamation's
  // declination took a long sentence down to 20 Hz.
  boundClausePitch(tokens, callerPitch);
}

} // namespace nvsp_frontend