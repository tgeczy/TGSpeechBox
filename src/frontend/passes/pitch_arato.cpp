/*
TGSpeechBox — Arató (BraiLab) intonation pass.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/
// =============================================================================
// Arató-style intonation — the BraiLab sentence melodies
// =============================================================================
//
// Reference: Arató András, "A BraiLab beszélő számítógépcsalád" (kandidátusi
// értekezés, műszaki leírás, Budapest, 1992), §5.4 "Mikrointonáció,
// intonáció, ének".  The BraiLab PC talker itself is the work of Vaspöri
// Teréz and Arató András ("BraiLab PC (c) Vaspöri Teréz és Arató András
// munkája" is the program's own banner).  This pass exists to carry that work
// forward: the shapes below are not invented, they were measured.
//
// What Arató describes (§5.4): the melody exists so a blind user hears, from
// the intonation alone, which punctuation mark closes the unit — and hears it
// even when the unit is cut off early, because the next line was already
// requested.  The MEA-8000 system had four types (two declarative, two
// question, imperative = wh-question melody); wh-questions were recognised by
// the sentence's first letter pairs HO HÁ MI ME KI; yes/no questions were
// split by length.  The PCF-8200 system added the syllable-count distinction.
//
// Where the numbers come from: frame captures of the 1991 TALKHUN program
// running in an emulator (C:\git\Brailab-wrapper\talkhun_emu, 2026-09-14),
// decoding the PCF8200 stream's start-pitch bytes and per-frame pitch
// increments into Hz.  Measured, unit start → shape → end:
//   declarative : 85 Hz, hump to 103 on the first syllable, straight decline
//                 to 82, plunge to 56 across the last two syllables.
//   yes/no      : 103 Hz, flat body, rise to 130–140 on the PENULTIMATE
//                 syllable, fall to ~90 on the last; long questions keep the
//                 body flat longer and start the rise later.
//   wh-question : 146 Hz at the start (the question word carries the peak),
//                 fall to ~113 by the end of the first word, decline to ~70,
//                 no terminal plunge.  Exclamations use the same shape.
//   comma clause: 112 Hz, straight decline to ~80; the next unit restarts at
//                 its own start pitch.  No continuation rise: the reset is
//                 the cue.
// Expressed here in semitones relative to each unit's start pitch, and the
// unit start pitches as semitone offsets from the voice's base pitch (the
// declarative start).  Every constant is a pack knob (arato*) so a language
// can re-measure and retune without touching this file.
//
// One call = one intonation unit (the platforms split text at punctuation and
// pass the closing mark as clauseType).  The contour is a list of (time,
// semitone) knots over the unit's spoken duration, anchored to syllables
// found from the syllable_marking pass, then sampled per token.

#include "pitch_arato.h"
#include "pitch_common.h"
#include "../ipa_engine.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nvsp_frontend {

namespace {

struct Knot {
  double timeMs;
  double semitones;
};

struct Syllable {
  int startIdx = -1;      // first token of the syllable
  int nucleusIdx = -1;    // first vowel token, -1 if none
  double startMs = 0.0;   // cumulative spoken time at the syllable start
  double nucleusEndMs = 0.0;
  double endMs = 0.0;
};

double interpolate(const std::vector<Knot>& knots, double t) {
  if (knots.empty()) return 0.0;
  if (t <= knots.front().timeMs) return knots.front().semitones;
  for (size_t k = 1; k < knots.size(); ++k) {
    const Knot& a = knots[k - 1];
    const Knot& b = knots[k];
    if (t <= b.timeMs) {
      const double span = b.timeMs - a.timeMs;
      if (span <= 0.0) return b.semitones;
      const double f = (t - a.timeMs) / span;
      return a.semitones + (b.semitones - a.semitones) * f;
    }
  }
  return knots.back().semitones;
}

// Knot times must be non-decreasing; anchors from short units can cross.
void addKnot(std::vector<Knot>& knots, double timeMs, double semitones) {
  if (!knots.empty() && timeMs < knots.back().timeMs) timeMs = knots.back().timeMs;
  knots.push_back({timeMs, semitones});
}

}  // namespace

void applyPitchArato(
    std::vector<Token>& tokens,
    const PackSet& pack,
    double speed,
    double basePitch,
    double inflection,
    char clauseType) {
  if (tokens.empty()) return;
  const auto& lang = pack.lang;
  const size_t n = tokens.size();
  (void)speed;  // durations arrive already rate-scaled; the shape is anchored to syllables

  // ---------------------------------------------------------------------------
  // Syllables and the time axis (cumulative spoken duration, silences excluded).
  // ---------------------------------------------------------------------------
  std::vector<Syllable> syl;
  std::vector<double> tokStartMs(n, 0.0);
  std::vector<double> tokEndMs(n, 0.0);
  double elapsed = 0.0;
  int firstSpoken = -1;
  int secondWordStart = -1;
  for (size_t i = 0; i < n; ++i) {
    const Token& t = tokens[i];
    tokStartMs[i] = elapsed;
    // Every token occupies time, including stop-closure gaps (which the
    // emitter renders as a voice bar and which must sit on the contour).
    elapsed += t.durationMs;
    tokEndMs[i] = elapsed;
    if (t.silence || !t.def) {
      if (!syl.empty()) syl.back().endMs = elapsed;
      continue;
    }
    if (firstSpoken < 0) firstSpoken = static_cast<int>(i);
    else if (secondWordStart < 0 && t.wordStart) secondWordStart = static_cast<int>(i);
    if (syl.empty() || t.syllableStart) {
      Syllable s;
      s.startIdx = static_cast<int>(i);
      s.startMs = tokStartMs[i];
      syl.push_back(s);
    }
    Syllable& cur = syl.back();
    if (cur.nucleusIdx < 0 && tokenIsVowel(t)) {
      cur.nucleusIdx = static_cast<int>(i);
      cur.nucleusEndMs = elapsed;
    }
    cur.endMs = elapsed;
  }
  const double T = elapsed;
  if (firstSpoken < 0 || T <= 0.0) {
    for (size_t i = 0; i < n; ++i) setPitchFields(tokens[i], basePitch, basePitch);
    return;
  }
  // A syllable with no vowel is a stray consonant cluster: fold it into the
  // previous one so the count matches what a listener hears.
  {
    std::vector<Syllable> folded;
    for (const Syllable& s : syl) {
      if (s.nucleusIdx < 0 && !folded.empty()) { folded.back().endMs = s.endMs; continue; }
      folded.push_back(s);
    }
    if (!folded.empty()) syl.swap(folded);
  }
  const int N = static_cast<int>(syl.size());

  // ---------------------------------------------------------------------------
  // Unit type.  Questions: Arató's word-initial pairs pick the wh-melody.
  // ---------------------------------------------------------------------------
  enum class Kind { Declarative, YesNo, Wh, Exclamation, Comma };
  Kind kind = Kind::Declarative;
  if (clauseType == '?') {
    kind = Kind::YesNo;
    // First two spoken phoneme keys of the unit against the pack's pairs.
    std::u32string k0, k1;
    int found = 0;
    for (size_t i = static_cast<size_t>(firstSpoken); i < n && found < 2; ++i) {
      const Token& t = tokens[i];
      if (t.silence || !t.def) continue;
      if (found == 0) k0 = t.def->key; else k1 = t.def->key;
      ++found;
    }
    for (const auto& pr : lang.aratoWhPairs) {
      if (pr.first == k0 && pr.second == k1) { kind = Kind::Wh; break; }
    }
  } else if (clauseType == '!') {
    kind = Kind::Exclamation;
  } else if (clauseType == ',' || clauseType == ';' || clauseType == ':') {
    kind = Kind::Comma;
  }

  // ---------------------------------------------------------------------------
  // Anchors.
  // ---------------------------------------------------------------------------
  const double nuc1End = syl.front().nucleusEndMs > 0.0 ? syl.front().nucleusEndMs : syl.front().endMs;
  const double lastStart = syl.back().startMs;
  const double penultStart = (N >= 2) ? syl[static_cast<size_t>(N - 2)].startMs : 0.0;
  const double firstWordEnd = (secondWordStart >= 0) ? tokStartMs[static_cast<size_t>(secondWordStart)] : T;

  // ---------------------------------------------------------------------------
  // Shape, in semitones relative to the unit start.
  // ---------------------------------------------------------------------------
  std::vector<Knot> knots;
  double startSt = 0.0;
  switch (kind) {
    case Kind::Declarative: {
      // Hump on the first syllable, straight decline, plunge over the last two
      // syllables (capped so a long final syllable does not plunge for a second).
      double plungeStart = (N >= 3) ? penultStart : lastStart;
      const double maxFall = lang.aratoFinalFallMaxMs / std::max(0.05, speed);
      if (T - plungeStart > maxFall) plungeStart = T - maxFall;
      addKnot(knots, 0.0, 0.0);
      addKnot(knots, nuc1End, lang.aratoHumpSt);
      addKnot(knots, plungeStart, lang.aratoBodyEndSt);
      addKnot(knots, T, lang.aratoFinalEndSt);
      break;
    }
    case Kind::YesNo: {
      startSt = lang.aratoQuestionStartSt;
      addKnot(knots, 0.0, 0.0);
      if (N <= 1) {
        // One syllable: rise-fall inside it.
        addKnot(knots, T * 0.5, lang.aratoQuestionPeakSt);
        addKnot(knots, T, lang.aratoQuestionEndSt);
      } else {
        const bool longUnit = (N >= lang.aratoLongUnitSyllables);
        if (longUnit) {
          // Long question: flat body, then a gentle pre-rise before the penult.
          addKnot(knots, T * 0.7, lang.aratoQuestionBodySt);
          addKnot(knots, penultStart, lang.aratoLongPreRiseSt);
        } else {
          addKnot(knots, penultStart, lang.aratoQuestionBodySt);
        }
        addKnot(knots, lastStart, lang.aratoQuestionPeakSt);   // rise across the penultimate syllable
        addKnot(knots, T, lang.aratoQuestionEndSt);            // fall across the last
      }
      break;
    }
    case Kind::Wh:
    case Kind::Exclamation: {
      startSt = (kind == Kind::Wh) ? lang.aratoWhStartSt : lang.aratoExclStartSt;
      // BraiLab has shed most of the peak by a fifth of the unit whatever the
      // first word's length (wh 146 -> 113, exclamation 146 -> 100 by decile 2).
      addKnot(knots, 0.0, 0.0);
      const double firstFall = std::min(firstWordEnd, T * 0.2);
      addKnot(knots, firstFall, lang.aratoWhFirstWordEndSt);
      addKnot(knots, std::max(firstFall, T * 0.3), lang.aratoWhMidSt);
      addKnot(knots, T, lang.aratoWhEndSt);
      break;
    }
    case Kind::Comma: {
      startSt = lang.aratoCommaStartSt;
      addKnot(knots, 0.0, 0.0);
      addKnot(knots, T, lang.aratoCommaEndSt);
      break;
    }
  }

  // ---------------------------------------------------------------------------
  // Sample per token.  Inflection scales every offset (the user's slider);
  // unvoiced and silent tokens carry the last pitch so nothing jumps.
  // ---------------------------------------------------------------------------
  // The measured shapes are reproduced at aratoInflectionRef (the platforms'
  // default slider position, 0.5); the slider scales them from there.
  const double ref = (lang.aratoInflectionRef > 0.0) ? lang.aratoInflectionRef : 0.5;
  const double scale = inflection / ref;
  auto hz = [&](double timeMs) {
    const double st = (startSt + interpolate(knots, timeMs)) * scale;
    return basePitch * std::pow(2.0, st / 12.0);
  };
  for (size_t i = 0; i < n; ++i) {
    Token& t = tokens[i];
    // Gaps and silences get the contour too: a voiced closure is on it, and
    // a true silence is inaudible either way.
    setPitchFields(t, hz(tokStartMs[i]), hz(tokEndMs[i]));
  }
}

} // namespace nvsp_frontend
