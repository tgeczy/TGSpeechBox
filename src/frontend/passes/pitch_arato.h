/*
TGSpeechBox — Arató (BraiLab) intonation pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_PASS_PITCH_ARATO_H
#define TGSB_PASS_PITCH_ARATO_H

#include <vector>
#include "../ipa_engine.h"

namespace nvsp_frontend {

// Arató-style intonation (legacyPitchMode = "arato_style").
//
// Models the sentence melodies of the BraiLab talking computers designed by
// Arató András and Vaspöri Teréz: one intonation unit per call, the unit's
// type read from its closing punctuation (and, for questions, from Arató's
// word-initial letter pairs), the contour a piecewise-linear shape in
// semitones anchored to the unit's syllables.  See pitch_arato.cpp for the
// reference and the measured constants.
void applyPitchArato(
  std::vector<Token>& tokens,
  const PackSet& pack,
  double speed,
  double basePitch,
  double inflection,
  char clauseType
);

} // namespace nvsp_frontend

#endif // TGSB_PASS_PITCH_ARATO_H
