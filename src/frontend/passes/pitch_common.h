/*
TGSpeechBox — Shared pitch mode utilities.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_PASS_PITCH_COMMON_H
#define TGSB_PASS_PITCH_COMMON_H

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>
#include "../ipa_engine.h"

namespace nvsp_frontend {

// Token classification helpers used by pitch modes.
// tokenIsVowel() is already in ipa_engine.h; tokenIsVoiced() is only in
// ipa_engine.cpp as a file-static, so we provide it here for the split-out
// pitch files.
inline bool pitchTokenIsVoiced(const Token& t) {
  return t.def && ((t.def->flags & kIsVoiced) != 0);
}

// Convert a percent (0–100 scale, 50 = basePitch) to Hz.
// 50 = basePitch, 100 = up one octave, 0 = down one octave, scaled by inflection.
inline double pitchFromPercent(double basePitch, double inflection, double percent) {
  const double exp = (((percent - 50.0) / 50.0) * inflection);
  return basePitch * std::pow(2.0, exp);
}

// Inverse of pitchFromPercent.
inline double percentFromPitch(double basePitch, double inflection, double pitch) {
  if (basePitch <= 0.0) return 50.0;
  if (inflection == 0.0) return 50.0;
  const double ratio = pitch / basePitch;
  if (ratio <= 0.0) return 50.0;
  const double exp = std::log2(ratio);
  return 50.0 + (50.0 * exp / inflection);
}

// Set voicePitch and endVoicePitch on a token, updating setMask.
inline void setPitchFields(Token& t, double startPitch, double endPitch) {
  const int vp = static_cast<int>(FieldId::voicePitch);
  const int evp = static_cast<int>(FieldId::endVoicePitch);
  t.field[vp] = startPitch;
  t.field[evp] = endPitch;
  t.setMask |= (1ull << vp);
  t.setMask |= (1ull << evp);
}

// The inflection scale's normal position: what the packs ship, and the value
// at which the pitch modes that take the scale as a factor are unchanged.
constexpr double kNormalInflectionScale = 0.58;

// How far a clause's pitch may go from the pitch the caller asked for, in the
// classic, Fujisaki, Klatt and impulse modes (#155).  Their movements are
// sized for a 110 Hz voice at half inflection; at 60 Hz and full inflection
// the Klatt mode's falls, which are in Hz, ended a statement at a third of
// the pitch, and at the inflection scale's top the classic question divided
// by zero.
// The numbers are a choice, not a measurement: a statement is reported to
// end at about 0.77 of its baseline (110 to 85 Hz) and a yes/no question at
// 1.4 to 1.5 times it (125 Hz to 170-190 Hz) in O'Shaughnessy 1976,
// "Modelling fundamental frequency, and its relationship to syntax,
// semantics, and phonetics", PhD thesis, MIT; these leave room below and
// above that and stop short of an octave down or more than an octave up.
constexpr double kPitchFloorRatio = 0.6;
constexpr double kPitchCeilRatio = 2.0;

// Keep a clause's pitch targets inside that range.  When its lowest target
// is under the floor, every target below the caller's pitch is moved toward
// that pitch in proportion, so the lowest lands on the floor and what fell
// still falls; the same above for the ceiling.  A clause that stays inside
// the range is not touched.  A target that is not a number becomes the
// caller's pitch.
inline void boundClausePitch(std::vector<Token>& tokens, double callerPitch) {
  if (!(callerPitch > 0.0)) return;
  const int vp = static_cast<int>(FieldId::voicePitch);
  const int evp = static_cast<int>(FieldId::endVoicePitch);
  const std::uint64_t mask = (1ull << vp) | (1ull << evp);
  double lowest = callerPitch;
  double highest = callerPitch;
  for (Token& t : tokens) {
    if ((t.setMask & mask) != mask) continue;
    for (int f : {vp, evp}) {
      if (!std::isfinite(t.field[f])) t.field[f] = callerPitch;
      if (t.field[f] < lowest) lowest = t.field[f];
      if (t.field[f] > highest) highest = t.field[f];
    }
  }
  const double floor = callerPitch * kPitchFloorRatio;
  const double ceil = callerPitch * kPitchCeilRatio;
  const double below = (lowest < floor) ? (callerPitch - floor) / (callerPitch - lowest) : 1.0;
  const double above = (highest > ceil) ? (ceil - callerPitch) / (highest - callerPitch) : 1.0;
  if (below == 1.0 && above == 1.0) return;
  for (Token& t : tokens) {
    if ((t.setMask & mask) != mask) continue;
    for (int f : {vp, evp}) {
      const double d = t.field[f] - callerPitch;
      t.field[f] = callerPitch + d * ((d < 0.0) ? below : above);
    }
  }
}

// Apply a linear pitch path across a range of tokens, distributing pitch
// change proportionally across voiced duration.
inline void applyPitchPath(std::vector<Token>& tokens, int startIndex, int endIndex,
                           double basePitch, double inflection, int startPct, int endPct) {
  if (startIndex >= endIndex) return;

  const double startPitch = pitchFromPercent(basePitch, inflection, startPct);
  const double endPitch = pitchFromPercent(basePitch, inflection, endPct);

  double voicedDuration = 0.0;
  for (int i = startIndex; i < endIndex; ++i) {
    if (pitchTokenIsVoiced(tokens[i])) voicedDuration += tokens[i].durationMs;
  }

  if (voicedDuration <= 0.0) {
    for (int i = startIndex; i < endIndex; ++i) {
      setPitchFields(tokens[i], startPitch, startPitch);
    }
    return;
  }

  double curDuration = 0.0;
  const double delta = endPitch - startPitch;
  double curPitch = startPitch;

  for (int i = startIndex; i < endIndex; ++i) {
    Token& t = tokens[i];
    const double start = curPitch;

    if (pitchTokenIsVoiced(t)) {
      curDuration += t.durationMs;
      const double ratio = curDuration / voicedDuration;
      curPitch = startPitch + (delta * ratio);
    }

    setPitchFields(t, start, curPitch);
  }
}

} // namespace nvsp_frontend

#endif // TGSB_PASS_PITCH_COMMON_H
