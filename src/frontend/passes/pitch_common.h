/*
TGSpeechBox — Shared pitch mode utilities.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_PASS_PITCH_COMMON_H
#define TGSB_PASS_PITCH_COMMON_H

#include <cmath>
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
