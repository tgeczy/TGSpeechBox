#ifndef TGSB_PASS_PITCH_FUJISAKI_H
#define TGSB_PASS_PITCH_FUJISAKI_H

#include <vector>
#include "../ipa_engine.h"

namespace nvsp_frontend {

// Fujisaki-style pitch contour pass.
//
// Replaces the old calculatePitchesFujisaki() from ipa_engine.cpp.
// Key improvement: multiple phrase commands across the utterance instead
// of one phrase command + linear declination hack.
void applyPitchFujisaki(
  std::vector<Token>& tokens,
  const PackSet& pack,
  double speed,
  double basePitch,
  double inflection,
  char clauseType
);

} // namespace nvsp_frontend

#endif // TGSB_PASS_PITCH_FUJISAKI_H
