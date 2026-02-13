/*
TGSpeechBox — Length contrast pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_PASSES_LENGTH_CONTRAST_H
#define TGSB_FRONTEND_PASSES_LENGTH_CONTRAST_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Enforces vowel length floors/ceilings and gemination timing cues for languages
// with phonemic length contrasts.
bool runLengthContrast(
  PassContext& ctx,
  std::vector<Token>& tokens,
  std::string& outError
);

} // namespace nvsp_frontend::passes

#endif // TGSB_FRONTEND_PASSES_LENGTH_CONTRAST_H
