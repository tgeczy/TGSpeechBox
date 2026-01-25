#ifndef NVSP_FRONTEND_PASSES_LENGTH_CONTRAST_H
#define NVSP_FRONTEND_PASSES_LENGTH_CONTRAST_H

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

#endif // NVSP_FRONTEND_PASSES_LENGTH_CONTRAST_H
