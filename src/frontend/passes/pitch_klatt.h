/*
TGSpeechBox — Klatt hat-pattern pitch model pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_PASS_PITCH_KLATT_H
#define TGSB_PASS_PITCH_KLATT_H

#include <vector>
#include "../ipa_engine.h"

namespace nvsp_frontend {

// Klatt 1987 hat-pattern intonation model pass.
//
// Generates a pitch contour using a three-state hat pattern:
//   BEFORE_HAT  — declining baseline before first primary stress
//   ON_HAT      — raised plateau with per-stress peaks
//   AFTER_HAT   — post-fall region below baseline
//
// Clause-type variations: statements/exclamations fall at the end,
// questions rise, commas sustain a continuation rise.
void applyPitchKlatt(
  std::vector<Token>& tokens,
  const PackSet& pack,
  double speed,
  double basePitch,
  double inflection,
  char clauseType
);

} // namespace nvsp_frontend

#endif // TGSB_PASS_PITCH_KLATT_H
