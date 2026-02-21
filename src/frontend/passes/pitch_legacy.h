/*
TGSpeechBox — Legacy pitch mode pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_PASS_PITCH_LEGACY_H
#define TGSB_PASS_PITCH_LEGACY_H

#include <vector>
#include "../ipa_engine.h"

namespace nvsp_frontend {

// Legacy pitch contour pass.
//
// Extracted from calculatePitchesLegacy() in ipa_engine.cpp.
// Time-based declination with stress accents — produces the classic
// "screen reader" prosody that is more predictable at higher rates.
void applyPitchLegacy(
  std::vector<Token>& tokens,
  const PackSet& pack,
  double speed,
  double basePitch,
  double inflection,
  char clauseType
);

} // namespace nvsp_frontend

#endif // TGSB_PASS_PITCH_LEGACY_H
