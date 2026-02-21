/*
TGSpeechBox — eSpeak-style pitch model pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_PASS_PITCH_ESPEAK_H
#define TGSB_PASS_PITCH_ESPEAK_H

#include <vector>
#include "../ipa_engine.h"

namespace nvsp_frontend {

// eSpeak-style (ToBI-based) pitch contour pass.
//
// Extracted from the espeak_style branch of calculatePitches() in
// ipa_engine.cpp.  Uses IntonationClause parameters (pre-head, head,
// nucleus, tail) to shape pitch across stressed/unstressed regions.
void applyPitchEspeak(
  std::vector<Token>& tokens,
  const PackSet& pack,
  double speed,
  double basePitch,
  double inflection,
  char clauseType
);

} // namespace nvsp_frontend

#endif // TGSB_PASS_PITCH_ESPEAK_H
