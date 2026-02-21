/*
TGSpeechBox — Impulse pitch model pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_PASS_PITCH_IMPULSE_H
#define TGSB_PASS_PITCH_IMPULSE_H

#include <vector>
#include "../ipa_engine.h"

namespace nvsp_frontend {

// Impulse-style pitch contour pass.
//
// Inspired by the Wintalker speech synthesizer's pitch model: linear
// declination baseline with count-based additive stress peaks that decay
// back to the baseline.  A two-pole IIR smoothing filter removes
// discontinuities from the raw pitch targets.
void applyPitchImpulse(
  std::vector<Token>& tokens,
  const PackSet& pack,
  double speed,
  double basePitch,
  double inflection,
  char clauseType
);

} // namespace nvsp_frontend

#endif // TGSB_PASS_PITCH_IMPULSE_H
