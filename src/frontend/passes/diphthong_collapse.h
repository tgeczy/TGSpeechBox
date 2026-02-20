/*
TGSpeechBox — Diphthong collapse pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_PASSES_DIPHTHONG_COLLAPSE_H
#define TGSB_FRONTEND_PASSES_DIPHTHONG_COLLAPSE_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Collapses tied vowel pairs into single diphthong tokens with
// onset->offset formant trajectories emitted as micro-frames.
bool runDiphthongCollapse(
  PassContext& ctx,
  std::vector<Token>& tokens,
  std::string& outError
);

} // namespace nvsp_frontend::passes

#endif // TGSB_FRONTEND_PASSES_DIPHTHONG_COLLAPSE_H
